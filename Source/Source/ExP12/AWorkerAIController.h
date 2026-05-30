// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AWorkerAIController.generated.h"

UCLASS()
class EXP12_API AWorkerAIController : public AAIController
{
	GENERATED_BODY()

public:
	void StartWandering(FVector Center, float Radius);
	void StopWandering();

protected:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
	FVector WanderCenter;
	float WanderRadius = 500.f;
	bool bIsWandering = false;

	// 운반 태스크 단계 추적
	enum class ECarryPhase : uint8
	{
		None,
		MovingToMaterial,		// 자재로 이동 중
		PickingUp,				// 집기 애니메이션 재생 중 (정지)
		MovingToDestination		// 자재 들고 목적지로 이동 중
	};
	ECarryPhase CarryPhase = ECarryPhase::None;
	AActor* TargetMaterial = nullptr;
	int32 CarryStepsRemaining = 0;		// 남은 경유지 수 (0이 되면 자재 내려놓음)

	void MoveToNextRandomPoint();
	void BeginEdgeApproach();
	void BeginCarryTask();
	void StartMoveToCarryDestination();	// 집기 딜레이 후 목적지 이동 시작
	AActor* FindNearestMaterial();

	FTimerHandle RetryTimerHandle;
};
