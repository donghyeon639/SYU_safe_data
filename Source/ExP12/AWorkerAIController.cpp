// Fill out your copyright notice in the Description page of Project Settings.

#include "AWorkerAIController.h"
#include "AWorkerCharacter.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"

// 배회 시작 – 스폰 위치를 중심으로 반경 내에서 랜덤 이동
void AWorkerAIController::StartWandering(FVector Center, float Radius)
{
	WanderCenter = Center;
	WanderRadius = Radius;
	bIsWandering = true;
	MoveToNextRandomPoint();
}

// 배회 중단 (낙하 직전 호출)
void AWorkerAIController::StopWandering()
{
	bIsWandering = false;
	CarryPhase = ECarryPhase::None;
	TargetMaterial = nullptr;
	StopMovement();
}

// 목적지 도착 또는 실패 시 호출
void AWorkerAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	// 운반 태스크: 자재 위치에 도착 → 집어서 목적지로 이동
	if (CarryPhase == ECarryPhase::MovingToMaterial)
	{
		if (Result.IsSuccess() && TargetMaterial && TargetMaterial->GetAttachParentActor() == nullptr)
		{
			if (AAWorkerCharacter* Worker = Cast<AAWorkerCharacter>(GetPawn()))
				Worker->StartCarrying(TargetMaterial);

			// 집기 애니메이션 재생 동안 대기 후 이동 시작 (애니메이션 길이에 맞게 조정)
			CarryPhase = ECarryPhase::PickingUp;
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::StartMoveToCarryDestination, 2.5f, false);
		}
		else
		{
			// 이동 실패 또는 다른 캐릭터가 이미 집어간 경우 → 태스크 취소 후 배회 재개
			CarryPhase = ECarryPhase::None;
			TargetMaterial = nullptr;
			bIsWandering = true;
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::MoveToNextRandomPoint, 1.f, false);
		}
		return;
	}

	// 운반 태스크: 경유지 도착 → 남은 경유지 있으면 계속 이동, 없으면 내려놓고 배회
	if (CarryPhase == ECarryPhase::MovingToDestination)
	{
		// 경로 실패 시 다른 지점으로 재시도 (경로 없으면 그냥 다음 목적지 찾기)
		if (!Result.IsSuccess())
		{
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::StartMoveToCarryDestination, 1.f, false);
			return;
		}

		// 30% 확률: 자재 든 채 가장자리 접근
		if (FMath::RandRange(0, 9) < 3)
		{
			CarryPhase = ECarryPhase::None;
			TargetMaterial = nullptr;
			float Pause = FMath::RandRange(0.5f, 1.5f);
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::BeginEdgeApproach, Pause, false);
			return;
		}

		// 경유지 남아있으면 다음 지점으로 이동 (내려놓지 않음)
		if (CarryStepsRemaining > 0)
		{
			CarryStepsRemaining--;
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::StartMoveToCarryDestination, 0.5f, false);
			return;
		}

		// 최종 목적지 도착 → 자재 내려놓고 배회 재개
		CarryPhase = ECarryPhase::None;
		TargetMaterial = nullptr;

		if (AAWorkerCharacter* Worker = Cast<AAWorkerCharacter>(GetPawn()))
			Worker->StopCarrying();

		bIsWandering = true;
		float Pause = FMath::RandRange(3.0f, 8.0f);
		GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::MoveToNextRandomPoint, Pause, false);
		return;
	}

	if (!bIsWandering) return;

	if (Result.IsSuccess())
	{
		int32 Roll = FMath::RandRange(0, 9);
		if (Roll < 3)
		{
			// 30% 확률: 가장자리 접근
			float Pause = FMath::RandRange(0.5f, 1.5f);
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::BeginEdgeApproach, Pause, false);
			return;
		}
		if (Roll < 4)
		{
			// 20% 확률: 자재 운반 태스크
			float Pause = FMath::RandRange(0.5f, 1.5f);
			GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::BeginCarryTask, Pause, false);
			return;
		}
		// 60%: 일반 배회 계속
		float Pause = FMath::RandRange(3.0f, 8.0f);
		GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::MoveToNextRandomPoint, Pause, false);
	}
	else
	{
		// 경로 실패 시 3초 대기 후 재시도
		GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::MoveToNextRandomPoint, 3.0f, false);
	}
}

// NavMesh 위 랜덤 도달 가능 지점 선택 후 이동 명령
void AWorkerAIController::MoveToNextRandomPoint()
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys || !bIsWandering) return;

	FNavLocation NavLoc;
	if (NavSys->GetRandomReachablePointInRadius(WanderCenter, WanderRadius, NavLoc))
		MoveToLocation(NavLoc.Location, 80.f, true, true, true, true);
	else
		GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &AWorkerAIController::MoveToNextRandomPoint, 0.5f, false);
}

// NavMesh 없이 랜덤 방향으로 직접 걷기 → 캐릭터가 물리적으로 가장자리를 밟아 낙하
void AWorkerAIController::BeginEdgeApproach()
{
	if (CarryPhase != ECarryPhase::None) return;

	AAWorkerCharacter* Worker = Cast<AAWorkerCharacter>(GetPawn());
	if (!Worker) return;

	// Z 350 미만이면 가장자리 접근 시도하지 않고 배회 재개
	if (Worker->GetActorLocation().Z < 350.f)
	{
		bIsWandering = true;
		MoveToNextRandomPoint();
		return;
	}

	bIsWandering = false;
	StopMovement();

	FVector Dir = FVector(FMath::RandRange(-1.f, 1.f), FMath::RandRange(-1.f, 1.f), 0.f).GetSafeNormal();
	Worker->StartWalkToEdge(Dir);
}

// 태그 "ConstructionMaterial" 오브젝트 중 가장 가까운 것 반환
AActor* AWorkerAIController::FindNearestMaterial()
{
	TArray<AActor*> Materials;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("ConstructionMaterial"), Materials);

	AActor* Nearest = nullptr;
	float MinDist = FLT_MAX;
	FVector MyLoc = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;

	for (AActor* Material : Materials)
	{
		// 이미 다른 캐릭터가 들고있는 자재 제외
		if (!Material || Material->GetAttachParentActor() != nullptr) continue;

		float Dist = FVector::Dist(MyLoc, Material->GetActorLocation());
		if (Dist < MinDist)
		{
			MinDist = Dist;
			Nearest = Material;
		}
	}
	return Nearest;
}

// 집기 애니메이션 딜레이 후 목적지로 이동 시작 (NavMesh 기반 원거리 지점 선택)
void AWorkerAIController::StartMoveToCarryDestination()
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	FNavLocation NavLoc;

	if (NavSys && NavSys->GetRandomReachablePointInRadius(WanderCenter, WanderRadius * 2.f, NavLoc))
		MoveToLocation(NavLoc.Location, 80.f);
	else
		MoveToLocation(WanderCenter, 80.f);

	CarryPhase = ECarryPhase::MovingToDestination;
}

// 가장 가까운 자재로 이동 시작
void AWorkerAIController::BeginCarryTask()
{
	if (CarryPhase != ECarryPhase::None) return;

	// PutDown 애니메이션 중 또는 이미 운반 중이면 스킵
	if (AAWorkerCharacter* Worker = Cast<AAWorkerCharacter>(GetPawn()))
		if (Worker->GetWorkerState() == EWorkerState::Carrying) return;

	TargetMaterial = FindNearestMaterial();
	if (!TargetMaterial) return;	// 집을 수 있는 자재 없으면 스킵

	bIsWandering = false;
	CarryPhase = ECarryPhase::MovingToMaterial;
	CarryStepsRemaining = FMath::RandRange(2, 4);	// 2~4개 경유지 이동 후 내려놓음
	MoveToLocation(TargetMaterial->GetActorLocation(), 100.f);
}
