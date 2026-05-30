// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AWorkerCharacter.generated.h"

UENUM(BlueprintType)
enum class EWorkerState : uint8
{
	Wandering	UMETA(DisplayName = "Wandering"),
	Carrying	UMETA(DisplayName = "Carrying"),
	Falling		UMETA(DisplayName = "Falling")
};

UCLASS()
class EXP12_API AAWorkerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAWorkerCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Worker|Fall")
	void TriggerFall();

	void StartWalkToEdge(FVector Dir);
	void StartCarrying(AActor* Material);
	void StopCarrying();
	void ForceCleanup();	// 리셋 시 타이머/오브젝트 강제 정리

	EWorkerState GetWorkerState() const { return WorkerState; }
	float GetFallStartZ() const { return FallStartZ; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worker|Wander")
	float WanderRadius = 500.f;

	// BP에서 AS_CarryShoulders_Montage 할당
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worker|Carry")
	UAnimMontage* CarryMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worker|Carry")
	FName CarrySocketName = FName("CarrySocket");

	// BP에서 PutDown 섹션 길이에 맞게 조정
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worker|Carry")
	float PutDownDuration = 3.3f;

private:
	EWorkerState WorkerState = EWorkerState::Wandering;

	AActor* CarriedMaterial = nullptr;

	bool bWalkingToEdge = false;
	FVector WalkToEdgeDir;
	FTimerHandle EdgeWalkTimerHandle;

	void StopWalkToEdge();
	void ActivateRagdoll();
	void EnterFallingState();   // TriggerFall/ConfirmFall 공통 전환 처리
	void ConfirmFall();
	void DetachCarriedMaterial();

	FTimerHandle FallConfirmTimerHandle;
	FTimerHandle DetachTimerHandle;
	FTimerHandle SpawnImmunityTimerHandle;

	bool bSpawnImmunity = true;	// 스폰 직후 낙하 감지 무시
	void ClearSpawnImmunity() { bSpawnImmunity = false; }
	void EnablePhysics();		// 스폰 2초 후 중력/이동 활성화 + AI 시작

	float FallStartZ = 0.f;		// 낙하 시작 Z 위치
	int32 PendingEdgeAccidentId = -1;	// bWalkingToEdge 즉시 캡처 시 발급된 사고 ID (-1 = 없음)

	TArray<USkeletalMeshComponent*> LeaderPoseFollowers;	// 래그돌 중 강제 갱신 대상
};
