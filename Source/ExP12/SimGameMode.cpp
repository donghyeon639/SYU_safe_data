// Fill out your copyright notice in the Description page of Project Settings.

#include "SimGameMode.h"
#include "AWorkerCharacter.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

ASimGameMode::ASimGameMode()
{
}

void ASimGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (SimUIClass)
	{
		UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), SimUIClass);
		if (Widget)
			Widget->AddToViewport();
	}

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}

	// 노멀/사고 카메라 두 세트 모두 생성 (BeginPlay에서 렌더러에 등록해야 정상 동작)
	InitCaptureActors();
}

bool ASimGameMode::IsFarEnough(FVector NewLoc, const TArray<FVector>& UsedLocations, float MinDist) const
{
	for (const FVector& Used : UsedLocations)
		if (FVector::Dist2D(NewLoc, Used) < MinDist)
			return false;
	return true;
}

bool ASimGameMode::GetRandomNavPointInXYRadius(UNavigationSystemV1* NavSys, FVector Center, float Radius, FNavLocation& OutLoc) const
{
	const int32 MaxRetries = 30;
	for (int32 i = 0; i < MaxRetries; i++)
	{
		if (!NavSys->GetRandomPointInNavigableRadius(Center, Radius, OutLoc)) continue;

		// 경사면 체크: 바닥 법선 Z < 0.95면 경사로로 판단하고 스킵
		FHitResult Hit;
		FVector TraceStart = OutLoc.Location + FVector(0, 0, 50);
		FVector TraceEnd   = OutLoc.Location - FVector(0, 0, 100);
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic))
		{
			if (Hit.Normal.Z < 0.95f) continue;
		}

		return true;
	}
	return false;
}

void ASimGameMode::StartSimulation(int32 NumWorkers)
{
	ClearWorkers();
	ClearMaterials();

	LastNumWorkers = NumWorkers;
	bAccidentOccurred = false;

	TArray<FVector> UsedLocations;
	SpawnMaterials(UsedLocations);
	SpawnDecorations(UsedLocations);

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys || !WorkerClass) return;

	for (int32 i = 0; i < NumWorkers; i++)
	{
		FNavLocation NavLoc;
		bool bFound = false;

		for (int32 Retry = 0; Retry < 50; Retry++)
		{
			if (!GetRandomNavPointInXYRadius(NavSys, SpawnCenter, SpawnRadius, NavLoc))
				break;

			if (IsFarEnough(NavLoc.Location, UsedLocations, 150.f))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound) continue;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		FRotator RandRot(0.f, FMath::RandRange(0.f, 360.f), 0.f);
		FVector SpawnLoc = NavLoc.Location + FVector(0, 0, 50.f);
		AActor* Worker = GetWorld()->SpawnActor<AActor>(WorkerClass, SpawnLoc, RandRot, Params);
		if (Worker)
		{
			UsedLocations.Add(NavLoc.Location);
			SpawnedWorkers.Add(Worker);
		}
	}
}

void ASimGameMode::ResetSimulation(int32 NumWorkers)
{
	StartSimulation(NumWorkers);
}

void ASimGameMode::CapturePlayerViewshot()
{
	PlayerCaptureCount++;

	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString SaveDir = FPaths::ProjectSavedDir() + TEXT("PlayerCaptures/")
	                + FString::Printf(TEXT("PlayerView_%04d_%s/"), PlayerCaptureCount, *Timestamp);
	IFileManager::Get().MakeDirectory(*SaveDir, true);

	// 플레이어 시점 스크린샷 - 절대 경로 지정으로 WindowsEditor 하위 폴더 우회
	FScreenshotRequest::RequestScreenshot(SaveDir + TEXT("screenshot.png"), false, false);

	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		PC->GetPlayerViewPoint(CamLoc, CamRot);

	FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"capture_id\": %d,\n")
		TEXT("  \"type\": \"player_view\",\n")
		TEXT("  \"timestamp\": \"%s\",\n")
		TEXT("  \"weather_preset\": \"%s\",\n")
		TEXT("  \"time_of_day\": %.1f,\n")
		TEXT("  \"worker_count\": %d,\n")
		TEXT("  \"camera_location\": {\"x\": %.1f, \"y\": %.1f, \"z\": %.1f},\n")
		TEXT("  \"camera_rotation\": {\"pitch\": %.1f, \"yaw\": %.1f, \"roll\": %.1f}\n")
		TEXT("}"),
		PlayerCaptureCount, *Timestamp, *CurrentWeatherPreset, CurrentTimeOfDay,
		SpawnedWorkers.Num(),
		CamLoc.X, CamLoc.Y, CamLoc.Z,
		CamRot.Pitch, CamRot.Yaw, CamRot.Roll
	);

	FFileHelper::SaveStringToFile(Json, *(SaveDir + TEXT("metadata.json")),
	                              FFileHelper::EEncodingOptions::ForceUTF8);
}

void ASimGameMode::SpawnConstructionSite()
{
	// 워커/자재를 먼저 정리해야 구조물 제거 시 낙하 감지 오발동 방지
	ClearWorkers();
	ClearMaterials();

	if (IsValid(SpawnedSite))
	{
		SpawnedSite->Destroy();
		SpawnedSite = nullptr;
	}
	for (TSubclassOf<AActor> SiteClass : ConstructionSiteClasses)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), SiteClass, Found);
		for (AActor* A : Found)
			A->Destroy();
	}

	if (ConstructionSiteClasses.Num() == 0) return;

	TSubclassOf<AActor> PickedClass = ConstructionSiteClasses[FMath::RandRange(0, ConstructionSiteClasses.Num() - 1)];
	if (!PickedClass) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedSite = GetWorld()->SpawnActor<AActor>(PickedClass, SiteSpawnLocation, FRotator::ZeroRotator, Params);

	NormalCaptureCenter = SpawnCenter;

	// 자동 루프 실행 중이면 새 구조물 위치 기준으로 노멀 카메라 재배치
	if (GetWorldTimerManager().IsTimerActive(NormalCaptureTimerHandle))
		StartNormalCameras();
}

void ASimGameMode::SpawnMaterials(TArray<FVector>& UsedLocations)
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys || !MaterialClass) return;

	const float MinDist = 150.f;
	const int32 MaxRetries = 10;

	for (int32 i = 0; i < NumMaterials; i++)
	{
		FNavLocation NavLoc;
		bool bFound = false;

		for (int32 Retry = 0; Retry < MaxRetries; Retry++)
		{
			if (!GetRandomNavPointInXYRadius(NavSys, MaterialSpawnCenter, MaterialSpawnRadius, NavLoc))
				break;
			if (IsFarEnough(NavLoc.Location, UsedLocations, MinDist))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound) continue;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AActor* Material = GetWorld()->SpawnActor<AActor>(MaterialClass, NavLoc.Location, FRotator::ZeroRotator, Params);
		if (Material)
		{
			Material->Tags.AddUnique(FName("ConstructionMaterial"));
			UsedLocations.Add(NavLoc.Location);
			SpawnedMaterials.Add(Material);
		}
	}
}

void ASimGameMode::SpawnDecorations(TArray<FVector>& UsedLocations)
{
	if (DecorationClasses.Num() == 0) return;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys) return;

	const float MinDist = 200.f;
	const int32 MaxRetries = 10;

	for (int32 i = 0; i < NumDecorations; i++)
	{
		TSubclassOf<AActor> PickedClass = DecorationClasses[FMath::RandRange(0, DecorationClasses.Num() - 1)];
		if (!PickedClass) continue;

		FNavLocation NavLoc;
		bool bFound = false;

		for (int32 Retry = 0; Retry < MaxRetries; Retry++)
		{
			if (!GetRandomNavPointInXYRadius(NavSys, MaterialSpawnCenter, MaterialSpawnRadius, NavLoc))
				break;
			if (IsFarEnough(NavLoc.Location, UsedLocations, MinDist))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound) continue;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		FRotator RandRot(0.f, FMath::RandRange(0.f, 360.f), 0.f);
		AActor* Deco = GetWorld()->SpawnActor<AActor>(PickedClass, NavLoc.Location, RandRot, Params);
		if (Deco)
		{
			// 장식 오브젝트는 물리 비활성화 (배경 전용)
			if (UStaticMeshComponent* SMC = Deco->FindComponentByClass<UStaticMeshComponent>())
				SMC->SetSimulatePhysics(false);

			UsedLocations.Add(NavLoc.Location);
			SpawnedMaterials.Add(Deco);
		}
	}
}

void ASimGameMode::ClearMaterials()
{
	for (AActor* Material : SpawnedMaterials)
	{
		if (IsValid(Material))
			Material->Destroy();
	}
	SpawnedMaterials.Empty();
}

void ASimGameMode::ClearWorkers()
{
	for (AActor* Worker : SpawnedWorkers)
	{
		if (!IsValid(Worker)) continue;

		if (AAWorkerCharacter* WC = Cast<AAWorkerCharacter>(Worker))
			WC->ForceCleanup();

		Worker->Destroy();
	}
	SpawnedWorkers.Empty();
}

// BeginPlay에서 호출 - 노멀 세트(4개) + 사고 세트(4개) 총 8개 SceneCapture2D 생성
void ASimGameMode::InitCaptureActors()
{
	const int32 TexWidth = 1280;
	const int32 TexHeight = 720;

	auto SpawnSet = [&](TArray<ASceneCapture2D*>& Actors, TArray<UTextureRenderTarget2D*>& RTs)
	{
		for (int32 i = 0; i < 4; i++)
		{
			ASceneCapture2D* Capture = GetWorld()->SpawnActor<ASceneCapture2D>(
				FVector(0.f, 0.f, -10000.f), FRotator::ZeroRotator);
			if (!Capture) continue;

			USceneCaptureComponent2D* Comp = Capture->GetCaptureComponent2D();
			Comp->FOVAngle = 90.f;
			Comp->bCaptureEveryFrame = false;
			Comp->bCaptureOnMovement = false;
			Comp->bAlwaysPersistRenderingState = true;
			Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Comp->ShowFlags.SetTemporalAA(false);
			Comp->ShowFlags.SetMotionBlur(false);
			Comp->ShowFlags.SetAmbientOcclusion(false);
			// UltraDynamicSky: SceneCapture 기본 비활성 항목 명시적 활성화
			Comp->ShowFlags.SetAtmosphere(true);
			Comp->ShowFlags.SetSkyLighting(true);
			// Lumen GI가 SceneCapture2D에서 기본 비활성 → 명시적으로 켜야 간접광이 캡처에 포함됨
			Comp->ShowFlags.SetGlobalIllumination(true);

			// UDS PostProcess Volume 설정 그대로 상속 (Method/Bias 직접 오버라이드 시 UDS 설정 파괴됨)
			// SpeedUp/Down: 카메라 이동 후 첫 프레임에서 즉시 노출 수렴
			// MinBrightness: 어두운 영역을 보는 카메라가 AE를 너무 올려 밝은 요소가 클리핑되는 현상 방지
			// MaxBrightness: 너무 밝은 씬에서 AE가 지나치게 내려가 전체가 어두워지는 현상 방지
			Comp->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
			Comp->PostProcessSettings.AutoExposureSpeedUp = 65536.f;
			Comp->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
			Comp->PostProcessSettings.AutoExposureSpeedDown = 65536.f;
			Comp->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
			Comp->PostProcessSettings.AutoExposureMinBrightness = 0.5f;
			Comp->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
			Comp->PostProcessSettings.AutoExposureMaxBrightness = 4.0f;

			UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(
				GetWorld(), TexWidth, TexHeight, ETextureRenderTargetFormat::RTF_RGBA8);
			Comp->TextureTarget = RT;

			Actors.Add(Capture);
			RTs.Add(RT);
		}
	};

	SpawnSet(NormalCaptureActors, NormalCaptureRTs);
	SpawnSet(AccidentCaptureActors, AccidentCaptureRTs);
}

// LineTrace로 벽 충돌 시 안전한 카메라 위치 반환 (건물 내부 배치 및 과근접 방지)
FVector ASimGameMode::FindSafeCameraPosition(const FVector& FocusPoint, const FVector& DesiredPos) const
{
	FVector TraceStart = FocusPoint + FVector(0.f, 0.f, 80.f);
	FHitResult Hit;
	FCollisionQueryParams Params(NAME_None, false);

	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, DesiredPos, ECC_WorldStatic, Params))
	{
		float SafeDist = Hit.Distance - 150.f;

		// 300cm 미만으로 가까워지면 수직 상방 폴백 (벽에 막혀 워커에 너무 근접하는 경우)
		if (SafeDist < 300.f)
		{
			FVector VertPos = FocusPoint + FVector(0.f, 0.f, 700.f);
			FHitResult VertHit;
			if (!GetWorld()->LineTraceSingleByChannel(VertHit, TraceStart, VertPos, ECC_WorldStatic, Params))
				return VertPos;
			// 수직도 막히면 천장 바로 아래
			return TraceStart + FVector(0.f, 0.f, FMath::Max(VertHit.Distance - 100.f, 100.f));
		}

		FVector Dir = (DesiredPos - TraceStart).GetSafeNormal();
		return TraceStart + Dir * SafeDist;
	}
	return DesiredPos;
}

// StartAutoLoop 시작 시 호출 - NormalCaptureCenter 기준 4방향 배치 + bCaptureEveryFrame 시작
// 사고 카메라도 같은 위치에 파킹 → AutoLoop 내내 Lumen 수렴 유지 → 낙하 시 즉시 재배치 가능
void ASimGameMode::StartNormalCameras()
{
	const FVector DirOffsets[] =
	{
		FVector(0.f,                  NormalCaptureRadius, 0.f),
		FVector( NormalCaptureRadius, 0.f,                 0.f),
		FVector(0.f,                 -NormalCaptureRadius, 0.f),
		FVector(-NormalCaptureRadius, 0.f,                 0.f),
	};

	for (int32 i = 0; i < 4; i++)
	{
		FVector CamPos = NormalCaptureCenter + DirOffsets[i] + FVector(0.f, 0.f, NormalCaptureHeight);
		FRotator CamRot = (NormalCaptureCenter - CamPos).Rotation();

		if (IsValid(NormalCaptureActors[i]))
		{
			NormalCaptureActors[i]->SetActorLocationAndRotation(CamPos, CamRot);
			NormalCaptureActors[i]->GetCaptureComponent2D()->bCaptureEveryFrame = true;
		}
		if (IsValid(AccidentCaptureActors[i]))
		{
			AccidentCaptureActors[i]->SetActorLocationAndRotation(CamPos, CamRot);
			AccidentCaptureActors[i]->GetCaptureComponent2D()->bCaptureEveryFrame = true;
		}
	}
}

// StopAutoLoop 시 호출 - bCaptureEveryFrame 중지 + 맵 외부로 복귀
void ASimGameMode::StopNormalCameras()
{
	for (ASceneCapture2D* Capture : NormalCaptureActors)
	{
		if (!IsValid(Capture)) continue;
		Capture->GetCaptureComponent2D()->bCaptureEveryFrame = false;
		Capture->SetActorLocation(FVector(0.f, 0.f, -10000.f));
	}
	for (ASceneCapture2D* Capture : AccidentCaptureActors)
	{
		if (!IsValid(Capture)) continue;
		Capture->GetCaptureComponent2D()->bCaptureEveryFrame = false;
		Capture->SetActorLocation(FVector(0.f, 0.f, -10000.f));
	}
}

// OnWorkerFell 시 호출 - 낙하 위치 기준 배치 + bCaptureEveryFrame으로 Lumen 워밍업 시작
// 사고 캡처 진행 중이면 호출 무시 (다른 캐릭터의 pre-warm이 카메라를 탈취하지 않도록)
void ASimGameMode::PositionAccidentCameras(const FVector& FallLocation)
{
	static const FVector DirOffsets[] =
	{
		FVector(0.f,    400.f, 0.f),
		FVector( 400.f,  0.f,  0.f),
		FVector(0.f,   -400.f, 0.f),
		FVector(-400.f,  0.f,  0.f),
	};

	if (bAccidentCamerasInUse) return;

	for (int32 i = 0; i < AccidentCaptureActors.Num(); i++)
	{
		if (!IsValid(AccidentCaptureActors[i])) continue;
		FVector DesiredPos = FallLocation + DirOffsets[i] + FVector(0.f, 0.f, 600.f);
		FVector CamPos = FindSafeCameraPosition(FallLocation, DesiredPos);
		FRotator CamRot = (FallLocation - CamPos).Rotation();
		AccidentCaptureActors[i]->SetActorLocationAndRotation(CamPos, CamRot);
		AccidentCaptureActors[i]->GetCaptureComponent2D()->bCaptureEveryFrame = true;
	}
}

// 사고 완료/취소 시 호출 - 노멀 카메라 위치로 복귀 (bCaptureEveryFrame 유지 → Lumen 계속 수렴)
// StopNormalCameras 호출 시 비로소 꺼짐
// 플래그 해제 → 다음 사고 캡처 허용
void ASimGameMode::StopAccidentCameras()
{
	const FVector DirOffsets[] =
	{
		FVector(0.f,                  NormalCaptureRadius, 0.f),
		FVector( NormalCaptureRadius, 0.f,                 0.f),
		FVector(0.f,                 -NormalCaptureRadius, 0.f),
		FVector(-NormalCaptureRadius, 0.f,                 0.f),
	};

	bAccidentCamerasInUse = false;

	for (int32 i = 0; i < 4 && i < AccidentCaptureActors.Num(); i++)
	{
		if (!IsValid(AccidentCaptureActors[i])) continue;
		FVector CamPos = NormalCaptureCenter + DirOffsets[i] + FVector(0.f, 0.f, NormalCaptureHeight);
		AccidentCaptureActors[i]->SetActorLocationAndRotation(CamPos, (NormalCaptureCenter - CamPos).Rotation());
	}
}

// 낙하 감지 시 호출 - 사고 카메라 워밍업 시작 + t=0.5/1.5/4.0초 캡처 예약
int32 ASimGameMode::OnWorkerFell(AActor* Worker)
{
	if (!IsValid(Worker)) return -1;
	if (bAccidentCamerasInUse) return -1;	// 카메라 사용 중 → 이 사고는 스킵

	bAccidentOccurred = true;
	AccidentCount++;
	int32 AccidentId = AccidentCount;

	FAccidentInfo Info;
	Info.Timestamp     = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	Info.FallLocation  = Worker->GetActorLocation();
	Info.FallStartZ    = 0.f;
	if (AAWorkerCharacter* WC = Cast<AAWorkerCharacter>(Worker))
		Info.FallStartZ = WC->GetFallStartZ();
	Info.WeatherPreset = CurrentWeatherPreset;
	Info.TimeOfDay     = CurrentTimeOfDay;
	AccidentInfoMap.Add(AccidentId, Info);

	// 낙하 위치 기준 배치 - bAccidentCamerasInUse 설정 전 호출해야 내부 체크 통과
	PositionAccidentCameras(Info.FallLocation);
	bAccidentCamerasInUse = true;

	static const float CaptureTimes[] = { 0.1f, 0.5f, 5.0f };
	static const float TimeLabels[]   = { 0.1f, 0.5f, 5.0f };

	TArray<FTimerHandle>& Handles = PendingCaptureTimers.Add(AccidentId);
	for (int32 k = 0; k < 3; k++)
	{
		FTimerHandle Handle;
		FTimerDelegate Del;
		Del.BindUFunction(this, FName("CaptureAccidentScreenshots"), Worker, TimeLabels[k], AccidentId);
		GetWorldTimerManager().SetTimer(Handle, Del, CaptureTimes[k], false);
		Handles.Add(Handle);
	}

	// t=3.0s: 래그돌 최종 위치로 카메라 재배치 → 2.0초 Lumen+노출 워밍업 후 t=5.0s 캡처
	{
		FTimerHandle ReposHandle;
		FTimerDelegate ReposDel;
		ReposDel.BindUFunction(this, FName("RepositionAccidentCameras"), Worker);
		GetWorldTimerManager().SetTimer(ReposHandle, ReposDel, 3.0f, false);
		Handles.Add(ReposHandle);
	}

	return AccidentId;
}

// 오감지 판명 시 호출 - 예약 타이머 취소 + 저장된 폴더 삭제
void ASimGameMode::CancelAccident(int32 AccidentId)
{
	if (TArray<FTimerHandle>* Handles = PendingCaptureTimers.Find(AccidentId))
	{
		for (FTimerHandle& Handle : *Handles)
			GetWorldTimerManager().ClearTimer(Handle);
		PendingCaptureTimers.Remove(AccidentId);
	}

	if (FAccidentInfo* Info = AccidentInfoMap.Find(AccidentId))
	{
		FString AccidentFolderPath = FPaths::ProjectSavedDir() + TEXT("Accidents/") + Info->Timestamp;
		IFileManager::Get().DeleteDirectory(*AccidentFolderPath, false, true);
		AccidentInfoMap.Remove(AccidentId);
	}

	StopAccidentCameras();
	AccidentCount--;
}

void ASimGameMode::RepositionAccidentCameras(AActor* Worker)
{
	if (!IsValid(Worker)) return;

	// 착지 위치 기준으로 카메라 재배치 (낙하 시작점 기준 → 착지점 기준으로 이동)
	FVector LookAt = Worker->GetActorLocation();
	if (AAWorkerCharacter* WC = Cast<AAWorkerCharacter>(Worker))
	{
		if (USkeletalMeshComponent* Mesh = WC->GetMesh())
			LookAt = Mesh->Bounds.GetBox().GetCenter();
	}

	static const FVector DirOffsets[] =
	{
		FVector(0.f,    400.f, 0.f),
		FVector( 400.f,  0.f,  0.f),
		FVector(0.f,   -400.f, 0.f),
		FVector(-400.f,  0.f,  0.f),
	};

	for (int32 i = 0; i < AccidentCaptureActors.Num(); i++)
	{
		if (!IsValid(AccidentCaptureActors[i])) continue;
		FVector DesiredPos = LookAt + DirOffsets[i] + FVector(0.f, 0.f, 350.f);
		FVector CamPos = FindSafeCameraPosition(LookAt, DesiredPos);
		AccidentCaptureActors[i]->SetActorLocationAndRotation(CamPos, (LookAt - CamPos).Rotation());
	}
}

// 사고 카메라 세트로 4방향 캡처 + bbox 계산 + 기록 추가 (t=4.0s 완료 시 JSON 기록)
void ASimGameMode::CaptureAccidentScreenshots(AActor* Worker, float TimeOffsetSec, int32 AccidentId)
{
	if (!IsValid(Worker)) return;
	if (!AccidentInfoMap.Contains(AccidentId)) return;
	if (AccidentCaptureActors.Num() < 4) return;

	FAccidentInfo& Info = AccidentInfoMap[AccidentId];

	FString SaveDir = FPaths::ProjectSavedDir() + TEXT("Accidents/") + Info.Timestamp + TEXT("/");
	IFileManager::Get().MakeDirectory(*SaveDir, true);

	static const TCHAR* DirNames[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };

	for (int32 i = 0; i < 4; i++)
	{
		if (!IsValid(AccidentCaptureActors[i])) continue;

		USceneCaptureComponent2D* Comp = AccidentCaptureActors[i]->GetCaptureComponent2D();

		float OutMinX = 1.f, OutMinY = 1.f, OutMaxX = 0.f, OutMaxY = 0.f;
		bool bIsOnScreen = false;

		FVector CamPos = AccidentCaptureActors[i]->GetActorLocation();
		FRotator CamRot = AccidentCaptureActors[i]->GetActorRotation();
		const float HalfFOVRad = FMath::DegreesToRadians(Comp->FOVAngle * 0.5f);
		const float TanHalfFOV = FMath::Tan(HalfFOVRad);
		const float AspectRatio = 1280.f / 720.f;

		// 래그돌 후에는 캡슐이 고정되어 GetActorBounds가 실제 메시 범위 미반영 → 메시 직접 참조
		FVector Origin, BoxExtent;
		bool bUsedMesh = false;
		if (AAWorkerCharacter* WC = Cast<AAWorkerCharacter>(Worker))
		{
			if (USkeletalMeshComponent* Mesh = WC->GetMesh())
			{
				FBox MeshBox = Mesh->Bounds.GetBox();
				Origin    = MeshBox.GetCenter();
				BoxExtent = MeshBox.GetExtent();
				bUsedMesh = true;
			}
		}
		if (!bUsedMesh)
			Worker->GetActorBounds(true, Origin, BoxExtent);

		FVector Corners[8];
		for (int32 c = 0; c < 8; ++c)
		{
			Corners[c] = Origin + FVector(
				(c & 1 ? BoxExtent.X : -BoxExtent.X),
				(c & 2 ? BoxExtent.Y : -BoxExtent.Y),
				(c & 4 ? BoxExtent.Z : -BoxExtent.Z)
			);
		}

		for (const FVector& Corner : Corners)
		{
			// 월드 → 카메라 로컬 (UE: X=전방, Y=우, Z=상)
			FVector Local = CamRot.UnrotateVector(Corner - CamPos);
			if (Local.X <= 0.f) continue;

			// 원근 투영 → NDC [-1, 1] → 화면 [0, 1] (Y축 반전)
			float NDC_X = Local.Y / (Local.X * TanHalfFOV);
			float NDC_Y = Local.Z * AspectRatio / (Local.X * TanHalfFOV);
			float NormX = (NDC_X + 1.f) * 0.5f;
			float NormY = (1.f - NDC_Y) * 0.5f;

			OutMinX = FMath::Min(OutMinX, NormX);
			OutMinY = FMath::Min(OutMinY, NormY);
			OutMaxX = FMath::Max(OutMaxX, NormX);
			OutMaxY = FMath::Max(OutMaxY, NormY);
			bIsOnScreen = true;
		}

		// bCaptureEveryFrame으로 RT가 이미 최신 상태 → FlushRenderingCommands로 동기화 후 읽기
		FlushRenderingCommands();

		FImage Image;
		if (FImageUtils::GetRenderTargetImage(AccidentCaptureRTs[i], Image))
		{
			TArray64<uint8> JpegData;
			if (FImageUtils::CompressImage(JpegData, TEXT("JPEG"), Image, 85) && JpegData.Num() > 0)
			{
				FString FilePath = SaveDir + FString::Printf(TEXT("t%.1fs_%s.jpg"), TimeOffsetSec, DirNames[i]);
				FFileHelper::SaveArrayToFile(TArrayView<const uint8>(JpegData.GetData(), JpegData.Num()), *FilePath);
			}
		}

		FAccidentCaptureRecord NewRecord;
		NewRecord.FileName = FString::Printf(TEXT("t%.1fs_%s"), TimeOffsetSec, DirNames[i]);
		NewRecord.MinX = bIsOnScreen ? FMath::Clamp(OutMinX, 0.f, 1.f) : 0.f;
		NewRecord.MinY = bIsOnScreen ? FMath::Clamp(OutMinY, 0.f, 1.f) : 0.f;
		NewRecord.MaxX = bIsOnScreen ? FMath::Clamp(OutMaxX, 0.f, 1.f) : 0.f;
		NewRecord.MaxY = bIsOnScreen ? FMath::Clamp(OutMaxY, 0.f, 1.f) : 0.f;
		Info.CaptureRecords.Add(NewRecord);
	}

	if (TimeOffsetSec >= 5.0f)
	{
		WriteFinalAccidentJSON(AccidentId);
		StopAccidentCameras();
	}
}

// 모든 캡처(t=4.0s) 완료 후 호출 - 바운딩 박스 포함 JSON 기록, 메모리 정리
void ASimGameMode::WriteFinalAccidentJSON(int32 AccidentId)
{
	if (!AccidentInfoMap.Contains(AccidentId)) return;

	const FAccidentInfo& Info = AccidentInfoMap[AccidentId];
	FString SaveDir = FPaths::ProjectSavedDir() + TEXT("Accidents/") + Info.Timestamp + TEXT("/");

	TSharedPtr<FJsonObject> RootObj = MakeShareable(new FJsonObject());
	RootObj->SetNumberField("accident_id", AccidentId);
	RootObj->SetStringField("timestamp", Info.Timestamp);
	RootObj->SetStringField("weather", Info.WeatherPreset);
	RootObj->SetNumberField("time_of_day", Info.TimeOfDay);
	RootObj->SetNumberField("fall_start_z", Info.FallStartZ);
	RootObj->SetNumberField("fall_height_cm", Info.FallStartZ - Info.FallLocation.Z);

	TSharedPtr<FJsonObject> FallLocObj = MakeShareable(new FJsonObject());
	FallLocObj->SetNumberField("x", Info.FallLocation.X);
	FallLocObj->SetNumberField("y", Info.FallLocation.Y);
	FallLocObj->SetNumberField("z", Info.FallLocation.Z);
	RootObj->SetObjectField("fall_location", FallLocObj);

	TArray<TSharedPtr<FJsonValue>> RecordsArray;
	for (const FAccidentCaptureRecord& Rec : Info.CaptureRecords)
	{
		TSharedPtr<FJsonObject> RecObj = MakeShareable(new FJsonObject());
		RecObj->SetStringField("time_label", Rec.FileName);
		RecObj->SetNumberField("bbox_min_x", Rec.MinX);
		RecObj->SetNumberField("bbox_min_y", Rec.MinY);
		RecObj->SetNumberField("bbox_max_x", Rec.MaxX);
		RecObj->SetNumberField("bbox_max_y", Rec.MaxY);
		RecordsArray.Add(MakeShareable(new FJsonValueObject(RecObj)));
	}
	RootObj->SetArrayField("captures", RecordsArray);

	FString JsonStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonStr, *(SaveDir + TEXT("metadata.json")), FFileHelper::EEncodingOptions::ForceUTF8);

	AccidentInfoMap.Remove(AccidentId);
	PendingCaptureTimers.Remove(AccidentId);
}

// 사고 없는 일반 공사 장면 정기 캡처 - 노멀 카메라는 이미 배치/워밍업 완료 상태
void ASimGameMode::CaptureNormalScene()
{
	if (bAccidentOccurred) return;
	if (NormalCaptureActors.Num() < 4) return;

	NormalCaptureCount++;

	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString FolderName = FString::Printf(TEXT("Normal_%04d_%s"), NormalCaptureCount, *Timestamp);
	FString SaveDir = FPaths::ProjectSavedDir() + TEXT("NormalScreenshots/") + FolderName + TEXT("/");
	IFileManager::Get().MakeDirectory(*SaveDir, true);

	static const TCHAR* DirNames[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };

	for (int32 i = 0; i < 4; i++)
	{
		if (!IsValid(NormalCaptureActors[i])) continue;

		// bCaptureEveryFrame으로 RT가 이미 최신 상태 → FlushRenderingCommands로 동기화 후 읽기
		FlushRenderingCommands();

		FImage Image;
		if (FImageUtils::GetRenderTargetImage(NormalCaptureRTs[i], Image))
		{
			TArray64<uint8> JpegData;
			if (FImageUtils::CompressImage(JpegData, TEXT("JPEG"), Image, 85) && JpegData.Num() > 0)
			{
				FString FilePath = SaveDir + FString::Printf(TEXT("%s.jpg"), DirNames[i]);
				FFileHelper::SaveArrayToFile(TArrayView<const uint8>(JpegData.GetData(), JpegData.Num()), *FilePath);
			}
		}
	}

	FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"capture_id\": %d,\n")
		TEXT("  \"type\": \"normal\",\n")
		TEXT("  \"timestamp\": \"%s\",\n")
		TEXT("  \"weather_preset\": \"%s\",\n")
		TEXT("  \"time_of_day\": %.1f\n")
		TEXT("}"),
		NormalCaptureCount, *Timestamp,
		*CurrentWeatherPreset, CurrentTimeOfDay
	);
	FFileHelper::SaveStringToFile(Json, *(SaveDir + TEXT("metadata.json")), FFileHelper::EEncodingOptions::ForceUTF8);
}

// 자동화 루프 시작 - 노멀 카메라 배치 후 간격마다 환경 변경, 전체 시간 후 종료
void ASimGameMode::StartAutoLoop(int32 NumWorkers, float TotalMinutes, float IntervalMinutes)
{
	StopAutoLoop();
	LastNumWorkers = NumWorkers;
	NormalCaptureCount = 0;
	AutoLoopRemainingMinutes = TotalMinutes;

	// 노멀 카메라 배치 + bCaptureEveryFrame 시작 (Lumen 상시 워밍업)
	StartNormalCameras();

	GetWorldTimerManager().SetTimer(NormalCaptureTimerHandle, this, &ASimGameMode::CaptureNormalScene, NormalCaptureInterval, true);
	GetWorldTimerManager().SetTimer(AutoLoopCountdownTimerHandle, this, &ASimGameMode::AutoLoopCountdownTick, 60.f, true);

	AutoLoopTick();

	const float IntervalSec = IntervalMinutes * 60.f;
	const float TotalSec    = TotalMinutes * 60.f;
	GetWorldTimerManager().SetTimer(AutoLoopIntervalTimerHandle, this, &ASimGameMode::AutoLoopTick, IntervalSec, true);
	GetWorldTimerManager().SetTimer(AutoLoopEndTimerHandle,      this, &ASimGameMode::AutoLoopEnd,  TotalSec,    false);
}

void ASimGameMode::StopAutoLoop()
{
	GetWorldTimerManager().ClearTimer(AutoLoopIntervalTimerHandle);
	GetWorldTimerManager().ClearTimer(AutoLoopEndTimerHandle);
	GetWorldTimerManager().ClearTimer(NormalCaptureTimerHandle);
	GetWorldTimerManager().ClearTimer(AutoLoopCountdownTimerHandle);
	StopNormalCameras();
}

// 1분마다 호출 - 남은 시간 차감 후 BP 이벤트로 전달
void ASimGameMode::AutoLoopCountdownTick()
{
	AutoLoopRemainingMinutes -= 1.f;
	if (AutoLoopRemainingMinutes > 0.f)
		OnAutoMinuteElapsed(FMath::RoundToInt(AutoLoopRemainingMinutes));
}

// 인터벌마다 호출 - BP 날씨/환경 변경 후 AutoResetDelay 대기 → 시뮬레이션 리셋
void ASimGameMode::AutoLoopTick()
{
	OnAutoIntervalReset();

	FTimerHandle DelayHandle;
	GetWorldTimerManager().SetTimer(DelayHandle, this, &ASimGameMode::AutoLoopDelayedReset, AutoResetDelay, false);
}

void ASimGameMode::AutoLoopDelayedReset()
{
	ResetSimulation(LastNumWorkers);
}

void ASimGameMode::AutoLoopEnd()
{
	StopAutoLoop();
}
