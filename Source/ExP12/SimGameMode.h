// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
// generated.h는 항상 가장 마지막 include여야 합니다.
#include "SimGameMode.generated.h"

// --- 전방 선언 (Forward Declaration) ---
class UNavigationSystemV1;
class ASceneCapture2D;
class UTextureRenderTarget2D;
class APlayerController;
class UUserWidget;

/** 개별 캡처 기록 구조체 (바운딩 박스 좌표 포함) */
USTRUCT(BlueprintType)
struct FAccidentCaptureRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString FileName;

	// 화면 좌상단~우하단 좌표 (0~1 비율로 정규화)
	UPROPERTY(BlueprintReadOnly)
	float MinX = 0.f;
	UPROPERTY(BlueprintReadOnly)
	float MinY = 0.f;
	UPROPERTY(BlueprintReadOnly)
	float MaxX = 0.f;
	UPROPERTY(BlueprintReadOnly)
	float MaxY = 0.f;
};

/** 사고 전체 정보 구조체 */
USTRUCT(BlueprintType)
struct FAccidentInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Timestamp;

	UPROPERTY(BlueprintReadOnly)
	FVector FallLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float FallStartZ = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FString WeatherPreset = TEXT("Unknown");

	UPROPERTY(BlueprintReadOnly)
	float TimeOfDay = 1200.f;

	// 0s, 0.5s, 4s 캡처 기록 배열 (바운딩 박스 포함)
	UPROPERTY(BlueprintReadOnly)
	TArray<FAccidentCaptureRecord> CaptureRecords;
};

/** 게임모드 클래스 */
UCLASS()
class EXP12_API ASimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimGameMode();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StartSimulation(int32 NumWorkers);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void ResetSimulation(int32 NumWorkers);

	// UI 버튼에서 호출 - 공사장 구조물 스폰 (기존 구조물 제거 후 랜덤 선택)
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void SpawnConstructionSite();

	// R키 입력 시 호출 - 플레이어 시점 스크린샷 + JSON 저장
	UFUNCTION(BlueprintCallable, Category = "Simulation|Capture")
	void CapturePlayerViewshot();

protected:
	virtual void BeginPlay() override;

public:
	// BP_WorkerCharacter 클래스 할당
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Workers")
	TSubclassOf<APawn> WorkerClass;

	// WBP_SimUI 위젯 클래스 할당
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	TSubclassOf<UUserWidget> SimUIClass;

	// 워커 스폰 중심점
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Workers")
	FVector SpawnCenter = FVector::ZeroVector;

	// 워커 스폰 반경
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Workers")
	float SpawnRadius = 500.f;

	// 운반할 자재 클래스 (ConstructionMaterial 태그 자동 부여)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Materials")
	TSubclassOf<AActor> MaterialClass;

	// 자재 스폰 수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Materials")
	int32 NumMaterials = 5;

	// 배경 장식 클래스 배열 (랜덤으로 선택해서 배치)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Decoration")
	TArray<TSubclassOf<AActor>> DecorationClasses;

	// 장식 오브젝트 스폰 수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Decoration")
	int32 NumDecorations = 10;

	// 자재/장식 스폰 중심점
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Materials")
	FVector MaterialSpawnCenter = FVector::ZeroVector;

	// 자재/장식 스폰 반경
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Materials")
	float MaterialSpawnRadius = 800.f;

	// 공사장 구조물 프리셋 목록 (랜덤으로 하나 선택)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Site")
	TArray<TSubclassOf<AActor>> ConstructionSiteClasses;

	// 구조물 스폰 위치
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Site")
	FVector SiteSpawnLocation = FVector::ZeroVector;

	// ── 노멀 씬 자동 캡처 ──────────────────────────────────────────
	// 사고 없는 일반 공사 장면 캡처 간격 (초)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|NormalCapture")
	float NormalCaptureInterval = 10.f;

	// 노멀 캡처 카메라 중심점 (에디터에서 공사장 중심으로 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|NormalCapture")
	FVector NormalCaptureCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|NormalCapture")
	float NormalCaptureRadius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|NormalCapture")
	float NormalCaptureHeight = 700.f;

	UPROPERTY(BlueprintReadOnly, Category = "Simulation|NormalCapture")
	int32 NormalCaptureCount = 0;

	// ── 자동화 루프 ────────────────────────────────────────────────
	// WBP_SimUI에서 호출 - 전체 시간/간격(분) 설정 후 자동 반복
	UFUNCTION(BlueprintCallable, Category = "Simulation|Auto")
	void StartAutoLoop(int32 NumWorkers, float TotalMinutes, float IntervalMinutes);

	// 환경 재생성 후 NavMesh 빌드 완료 대기 시간 (짧으면 NavMesh 미완성 상태에서 캐릭터 스폰됨)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Auto")
	float AutoResetDelay = 4.0f;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Auto")
	void StopAutoLoop();

	// BP_SimGameMode에서 오버라이드 - 날씨/환경 랜덤화 로직 연결 지점
	UFUNCTION(BlueprintImplementableEvent, Category = "Simulation|Auto")
	void OnAutoIntervalReset();

	// 1분마다 호출 - BP에서 Print String으로 "X분 남았습니다" 표시
	UFUNCTION(BlueprintImplementableEvent, Category = "Simulation|Auto")
	void OnAutoMinuteElapsed(int32 RemainingMinutes);

	// ── 사고 캡처 ──────────────────────────────────────────────────
	// 사고 발생 시 호출 - 4방향 스크린샷 캡처 예약, 발급된 AccidentId 반환
	UFUNCTION(BlueprintCallable, Category = "Simulation|Accident")
	int32 OnWorkerFell(AActor* Worker);

	// 오감지 판명 시 호출 - 예약된 타이머 취소 + 저장된 파일/폴더 삭제
	void CancelAccident(int32 AccidentId);

	// StartWalkToEdge 시점에 호출 - 낙하 전 미리 배치해서 노출 수렴 확보
	void PositionAccidentCameras(const FVector& FallLocation);

	// 사고 누적 카운트 (외부 참조용)
	UPROPERTY(BlueprintReadOnly, Category = "Simulation|Accident")
	int32 AccidentCount = 0;

	// BP에서 날씨 랜덤화 시 선택된 프리셋 이름 갱신
	UPROPERTY(BlueprintReadWrite, Category = "Simulation|Accident")
	FString CurrentWeatherPreset = TEXT("Unknown");

	// BP에서 UDS Set Time Of Day 호출 시 동일 값 갱신
	UPROPERTY(BlueprintReadWrite, Category = "Simulation|Accident")
	float CurrentTimeOfDay = 1200.f;

private:
	TArray<AActor*> SpawnedWorkers;
	TArray<AActor*> SpawnedMaterials;
	AActor* SpawnedSite = nullptr;

	void ClearWorkers();
	void ClearMaterials();
	void SpawnMaterials(TArray<FVector>& UsedLocations);
	void SpawnDecorations(TArray<FVector>& UsedLocations);

	bool IsFarEnough(FVector NewLoc, const TArray<FVector>& UsedLocations, float MinDist) const;
	bool GetRandomNavPointInXYRadius(UNavigationSystemV1* NavSys, FVector Center, float Radius, FNavLocation& OutLoc) const;

	// LineTrace로 벽 충돌 시 안전한 카메라 위치 반환 (건물 내부 배치 방지)
	FVector FindSafeCameraPosition(const FVector& FocusPoint, const FVector& DesiredPos) const;

	// ── 노멀 캡처 전용 SceneCapture2D (StartAutoLoop 중 항상 렌더링 → Lumen 상시 수렴) ──
	UPROPERTY()
	TArray<ASceneCapture2D*> NormalCaptureActors;
	UPROPERTY()
	TArray<UTextureRenderTarget2D*> NormalCaptureRTs;

	// ── 사고 캡처 전용 SceneCapture2D (OnWorkerFell 시점부터 워밍업 시작) ──
	UPROPERTY()
	TArray<ASceneCapture2D*> AccidentCaptureActors;
	UPROPERTY()
	TArray<UTextureRenderTarget2D*> AccidentCaptureRTs;

	void InitCaptureActors();           // 두 세트 모두 초기화 (BeginPlay)
	void StartNormalCameras();          // NormalCaptureCenter 위치 배치 + bCaptureEveryFrame 시작
	void StopNormalCameras();           // bCaptureEveryFrame 중지 + -10000 복귀
	void StopAccidentCameras();         // 사고 완료/취소 시 정리

	// TimeOffsetSec: 낙하 시작 기준 경과 시간 (파일명에 표기), AccidentId: 동일 사고의 3회 캡처를 하나로 묶는 키
	UFUNCTION()
	void CaptureAccidentScreenshots(AActor* Worker, float TimeOffsetSec, int32 AccidentId);

	// t=3.5s에 호출 - 래그돌 현재 위치로 카메라 재배치 (t=4.0s 캡처 전 0.5초 Lumen 워밍업)
	UFUNCTION()
	void RepositionAccidentCameras(AActor* Worker);

	// 사고 정보 관리용 맵 (Key: AccidentId)
	TMap<int32, FAccidentInfo> AccidentInfoMap;
	TMap<int32, TArray<FTimerHandle>> PendingCaptureTimers;

	// t=4.0s 캡처 완료 후 최종 JSON 기록 (바운딩 박스 포함)
	void WriteFinalAccidentJSON(int32 AccidentId);

	int32 LastNumWorkers = 5;
	int32 PlayerCaptureCount = 0;
	bool bAccidentOccurred = false;
	bool bAccidentCamerasInUse = false;	// 사고 캡처 진행 중 → 다른 캐릭터의 카메라 탈취 방지

	FTimerHandle NormalCaptureTimerHandle;
	FTimerHandle AutoLoopIntervalTimerHandle;
	FTimerHandle AutoLoopEndTimerHandle;
	FTimerHandle AutoLoopCountdownTimerHandle;

	float AutoLoopRemainingMinutes = 0.f;

	void CaptureNormalScene();
	void AutoLoopTick();
	void AutoLoopDelayedReset();
	void AutoLoopEnd();
	void AutoLoopCountdownTick();
};
