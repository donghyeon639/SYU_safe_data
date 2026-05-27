// Fill out your copyright notice in the Description page of Project Settings.

#include "AWorkerCharacter.h"
#include "AWorkerAIController.h"
#include "SimGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

AAWorkerCharacter::AAWorkerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AWorkerAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->DefaultLandMovementMode = MOVE_Walking;
}

void AAWorkerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// NavMesh 빌드 미완성 상태에서 스폰 시 즉시 추락 방지 - 2초간 중력/이동 정지
	bSpawnImmunity = true;
	GetCharacterMovement()->GravityScale = 0.f;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	GetWorldTimerManager().SetTimer(SpawnImmunityTimerHandle, this, &AAWorkerCharacter::EnablePhysics, 1.f, false);
}

void AAWorkerCharacter::EnablePhysics()
{
	GetCharacterMovement()->GravityScale = 1.f;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	if (AWorkerAIController* AIC = Cast<AWorkerAIController>(GetController()))
		AIC->StartWandering(GetActorLocation(), WanderRadius);

	// 바닥 착지 과정 중 오감지 방지를 위해 1초 더 감지 유예
	GetWorldTimerManager().SetTimer(SpawnImmunityTimerHandle, this, &AAWorkerCharacter::ClearSpawnImmunity, 1.f, false);
}

void AAWorkerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 가장자리 접근 중: NavMesh 없이 직접 이동 입력으로 물리적으로 걸어가다 낙하
	if (bWalkingToEdge)
		AddMovementInput(WalkToEdgeDir);

	// 래그돌 중 Leader Pose 팔로워(장갑 등) 강제 갱신 - 물리 시뮬레이션 전환 시 끊기는 현상 방지
	if (WorkerState == EWorkerState::Falling)
	{
		for (USkeletalMeshComponent* SKM : LeaderPoseFollowers)
		{
			if (IsValid(SKM))
				SKM->SetLeaderPoseComponent(GetMesh(), true);
		}
	}
}

void AAWorkerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

// 리셋 시 호출 - 타이머 전부 취소하고 운반 오브젝트 즉시 분리
void AAWorkerCharacter::ForceCleanup()
{
	GetWorldTimerManager().ClearTimer(DetachTimerHandle);
	GetWorldTimerManager().ClearTimer(FallConfirmTimerHandle);
	GetWorldTimerManager().ClearTimer(EdgeWalkTimerHandle);

	if (CarriedMaterial)
	{
		CarriedMaterial->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (UStaticMeshComponent* SMC = CarriedMaterial->FindComponentByClass<UStaticMeshComponent>())
		{
			SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			SMC->SetSimulatePhysics(true);
		}
		CarriedMaterial = nullptr;
	}
}

// AIController에서 호출 - NavMesh 없이 랜덤 방향으로 직접 걷기 시작
void AAWorkerCharacter::StartWalkToEdge(FVector Dir)
{
	if (WorkerState != EWorkerState::Wandering && WorkerState != EWorkerState::Carrying) return;
	bWalkingToEdge = true;
	WalkToEdgeDir = Dir;

	// 낙하 전 미리 사고 카메라를 현재 위치로 배치 → 낙하까지 최대 6초간 노출 수렴
	if (ASimGameMode* GM = Cast<ASimGameMode>(UGameplayStatics::GetGameMode(this)))
		GM->PositionAccidentCameras(GetActorLocation());

	// 6초 내 추락 없으면 배회 재개
	GetWorldTimerManager().SetTimer(EdgeWalkTimerHandle, this, &AAWorkerCharacter::StopWalkToEdge, 6.f, false);
}

// timeout 만료 → 운반 중이면 자재 내려놓기, 이후 배회 재개
void AAWorkerCharacter::StopWalkToEdge()
{
	bWalkingToEdge = false;

	// 가장자리 접근 중 낙하 실패 시 자재 내려놓고 복귀 (NavMesh 가장자리에서 경로 실패 방지)
	if (WorkerState == EWorkerState::Carrying)
		StopCarrying();

	if (AWorkerAIController* AIC = Cast<AWorkerAIController>(GetController()))
		AIC->StartWandering(GetActorLocation(), WanderRadius);
}

// AIController에서 자재 도착 시 호출 - 소켓에 부착 후 몽타주 재생
void AAWorkerCharacter::StartCarrying(AActor* Material)
{
	if (!Material) return;
	if (CarriedMaterial) return;	// 이미 운반 중이면 무시
	WorkerState = EWorkerState::Carrying;
	CarriedMaterial = Material;

	// 물리 비활성화 후 충돌 끄기 (SetSimulatePhysics 활성 상태로 Attach 시 위치 덮어쓰기 방지)
	if (UStaticMeshComponent* SMC = Material->FindComponentByClass<UStaticMeshComponent>())
	{
		SMC->SetSimulatePhysics(false);
		SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 어깨 소켓에 자재 부착 (위치/회전만 스냅, 스케일은 원본 유지)
	bool bAttached = Material->AttachToComponent(GetMesh(),
		FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false),
		CarrySocketName);

	// 운반 몽타주 재생
	if (CarryMontage)
		PlayAnimMontage(CarryMontage);
}

// AIController에서 목적지 도착 시 호출 - PutDown 섹션 재생 후 자재 분리
void AAWorkerCharacter::StopCarrying()
{
	WorkerState = EWorkerState::Wandering;

	if (CarryMontage)
	{
		// PutDown 섹션으로 점프 후 재생 완료 시 분리
		if (UAnimInstance* AI = GetMesh()->GetAnimInstance())
			AI->Montage_JumpToSection(FName("PutDown"), CarryMontage);

		GetWorldTimerManager().SetTimer(DetachTimerHandle, this,
			&AAWorkerCharacter::DetachCarriedMaterial, PutDownDuration, false);
	}
	else
	{
		DetachCarriedMaterial();
	}
}

// PutDown 애니메이션 중 적절한 타이밍에 자재 분리 → 물리로 자연 낙하
void AAWorkerCharacter::DetachCarriedMaterial()
{
	if (!CarriedMaterial) return;

	// 현재 소켓 위치 그대로 분리 후 물리 낙하 (강제 위치 이동 없음)
	CarriedMaterial->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	if (UStaticMeshComponent* SMC = CarriedMaterial->FindComponentByClass<UStaticMeshComponent>())
	{
		SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SMC->SetSimulatePhysics(true);
	}
	CarriedMaterial = nullptr;
}

void AAWorkerCharacter::TriggerFall()
{
	if (WorkerState != EWorkerState::Wandering && WorkerState != EWorkerState::Carrying) return;

	// 운반 중 낙하 시 자재 분리
	if (CarriedMaterial)
	{
		CarriedMaterial->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (UStaticMeshComponent* SMC = CarriedMaterial->FindComponentByClass<UStaticMeshComponent>())
		{
			SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			SMC->SetSimulatePhysics(true);
		}
		CarriedMaterial = nullptr;
	}
	if (CarryMontage)
		StopAnimMontage(CarryMontage);

	WorkerState = EWorkerState::Falling;
	bWalkingToEdge = false;
	GetWorldTimerManager().ClearTimer(EdgeWalkTimerHandle);

	if (AWorkerAIController* AIC = Cast<AWorkerAIController>(GetController()))
		AIC->StopWandering();

	// bWalkingToEdge 즉시 캡처 시 이미 OnWorkerFell 호출됨 → 중복 방지
	if (PendingEdgeAccidentId < 0)
	{
		if (ASimGameMode* GM = Cast<ASimGameMode>(UGameplayStatics::GetGameMode(this)))
			GM->OnWorkerFell(this);
	}
	PendingEdgeAccidentId = -1;

	ActivateRagdoll();

	FVector RandomDir = FVector(FMath::RandRange(-1.f, 1.f), FMath::RandRange(-1.f, 1.f), 0.f).GetSafeNormal();
	GetMesh()->AddImpulse(RandomDir * 150.f + FVector(0, 0, 80.f), NAME_None, true);
}

void AAWorkerCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (WorkerState != EWorkerState::Wandering && WorkerState != EWorkerState::Carrying) return;

	if (GetCharacterMovement()->MovementMode == MOVE_Falling)
	{
		FallStartZ = GetActorLocation().Z;
		GetWorldTimerManager().SetTimer(FallConfirmTimerHandle, this, &AAWorkerCharacter::ConfirmFall, 0.3f, false);

		// 가장자리 의도 접근 중 낙하 시작 즉시 캡처 (난간에서 떨어지는 순간 포착)
		// 일반 배회 중 계단은 bWalkingToEdge=false라 제외됨
		if (!bSpawnImmunity && bWalkingToEdge)
		{
			if (ASimGameMode* GM = Cast<ASimGameMode>(UGameplayStatics::GetGameMode(this)))
				PendingEdgeAccidentId = GM->OnWorkerFell(this);
		}
	}
	else if (PrevMovementMode == MOVE_Falling)
	{
		GetWorldTimerManager().ClearTimer(FallConfirmTimerHandle);
		if (bSpawnImmunity) return;
		float FallDist = FallStartZ - GetActorLocation().Z;
		if (FallDist > 150.f)
		{
			TriggerFall();
		}
		else if (PendingEdgeAccidentId >= 0)
		{
			// 낙하 거리 150cm 이하 → 계단으로 판명, 오감지 데이터 삭제
			if (ASimGameMode* GM = Cast<ASimGameMode>(UGameplayStatics::GetGameMode(this)))
				GM->CancelAccident(PendingEdgeAccidentId);
			PendingEdgeAccidentId = -1;
		}
	}
}

// 1초 이상 낙하 중이면 실제 낙하로 판정 → 래그돌 활성화
void AAWorkerCharacter::ConfirmFall()
{
	if (bSpawnImmunity) return;
	if (GetCharacterMovement()->MovementMode != MOVE_Falling) return;
	if (WorkerState != EWorkerState::Wandering && WorkerState != EWorkerState::Carrying) return;

	if (CarriedMaterial)
	{
		CarriedMaterial->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (UStaticMeshComponent* SMC = CarriedMaterial->FindComponentByClass<UStaticMeshComponent>())
		{
			SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			SMC->SetSimulatePhysics(true);
		}
		CarriedMaterial = nullptr;
	}
	if (CarryMontage)
		StopAnimMontage(CarryMontage);

	WorkerState = EWorkerState::Falling;
	bWalkingToEdge = false;
	GetWorldTimerManager().ClearTimer(EdgeWalkTimerHandle);

	if (AWorkerAIController* AIC = Cast<AWorkerAIController>(GetController()))
		AIC->StopWandering();

	// bWalkingToEdge 즉시 캡처 시 이미 OnWorkerFell 호출됨 → 중복 방지
	if (PendingEdgeAccidentId < 0)
	{
		if (ASimGameMode* GM = Cast<ASimGameMode>(UGameplayStatics::GetGameMode(this)))
			GM->OnWorkerFell(this);
	}
	PendingEdgeAccidentId = -1;

	ActivateRagdoll();
}

void AAWorkerCharacter::ActivateRagdoll()
{
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetSimulatePhysics(true);

	// 래그돌 전환 시 Leader Pose 팔로워를 GetMesh()로 re-attach
	// (Actor 루트에 붙어있으면 GetMesh()가 물리로 이동해도 장갑이 제자리에 남아 사라짐)
	LeaderPoseFollowers.Empty();
	TArray<USkeletalMeshComponent*> ChildMeshes;
	GetComponents<USkeletalMeshComponent>(ChildMeshes);
	for (USkeletalMeshComponent* SKM : ChildMeshes)
	{
		if (SKM != GetMesh())
		{
			SKM->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepWorldTransform);
			LeaderPoseFollowers.Add(SKM);
		}
	}
}
