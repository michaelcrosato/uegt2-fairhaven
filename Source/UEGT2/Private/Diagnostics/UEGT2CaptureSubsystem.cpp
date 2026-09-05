#include "Diagnostics/UEGT2CaptureSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "Dev/UEGT2DevModeSubsystem.h"
#include "HAL/PlatformMemory.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/UEGT2Dialogue.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectGlobals.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "UEGT2LogChannels.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* CaptureSwitch = TEXT("UEGT2Capture=");

	FString GetStringArg(const TCHAR* Key, const FString& Default)
	{
		FString Value;
		if (FParse::Value(FCommandLine::Get(), Key, Value))
		{
			return Value;
		}
		return Default;
	}

	float GetFloatArg(const TCHAR* Key, float Default)
	{
		float Value = Default;
		FParse::Value(FCommandLine::Get(), Key, Value);
		return Value;
	}
}

bool UUEGT2CaptureSubsystem::IsCaptureRequested()
{
	FString Unused;
	return FParse::Value(FCommandLine::Get(), CaptureSwitch, Unused);
}

bool UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2SmokeWalk"));
}

bool UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureLife"))
		|| FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureSquareWashrooms"));
}

bool UUEGT2CaptureSubsystem::IsFlySoakRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2SmokeFly"));
}

const TArray<FUEGT2Viewpoint>& UUEGT2CaptureSubsystem::GetTour()
{
	// Coordinates follow the world convention: +X north, +Y east.
	// Keep this list in step with Tools/Terrain/world_config.py.
	static const TArray<FUEGT2Viewpoint> Points = []
	{
		TArray<FUEGT2Viewpoint> Result;
		auto Add = [&Result](const TCHAR* Name, float X, float Y, float Height,
			float Yaw, float Pitch, const TCHAR* Description)
		{
			FUEGT2Viewpoint Point;
			Point.Name = FName(Name);
			Point.Location = FVector2D(X, Y);
			Point.HeightAboveGround = Height;
			Point.Yaw = Yaw;
			Point.Pitch = Pitch;
			Point.Description = Description;
			Result.Add(Point);
		};

		// Indoors: Z is an absolute world height, and the coordinates come from
		// a house the content build placed. Like every other viewpoint in this
		// list they are tied to the current seed - Tools/Python/uegt2 has a
		// scratch script that re-derives them if the town ever moves.
		auto AddIndoor = [&Result](const TCHAR* Name, float X, float Y, float Z,
			float Yaw, float Pitch, const TCHAR* Description)
		{
			FUEGT2Viewpoint Point;
			Point.Name = FName(Name);
			Point.Location = FVector2D(X, Y);
			Point.HeightAboveGround = Z;
			Point.bAbsoluteHeight = true;
			Point.Yaw = Yaw;
			Point.Pitch = Pitch;
			Point.Description = Description;
			Result.Add(Point);
		};

		Add(TEXT("TownSquare"), 0.0f, 0.0f, 1.8f, 70.0f, -4.0f,
			TEXT("Town centre looking east toward the harbour"));
		Add(TEXT("MainStreet"), -6000.0f, -4000.0f, 1.8f, 55.0f, -2.0f,
			TEXT("Main street approach into town"));
		// Off the square itself, looking back across it. TownSquare stands the
		// camera on the well, four metres above everyone's head - a fine shot
		// of the roofs and a useless one of the people under them. Height is
		// measured from whatever the downward trace hits, so this sits a little
		// above the square rather than exactly in it.
		Add(TEXT("Market"), 2900.0f, 1500.0f, 1.8f, 207.0f, -3.0f,
			TEXT("The market square and the crowd around the well"));
		Add(TEXT("Waterfront"), -1200.0f, 26500.0f, 2.2f, 78.0f, -5.0f,
			TEXT("Shoreline and docks looking out to sea"));
		Add(TEXT("BeachSouth"), -26000.0f, 21000.0f, 2.0f, 20.0f, -3.0f,
			TEXT("Beach south of town looking back north"));
		Add(TEXT("Farmland"), -12000.0f, -46000.0f, 2.0f, 30.0f, -3.0f,
			TEXT("Farm road through the western fields"));
		Add(TEXT("MillPond"), -13000.0f, -37000.0f, 2.0f, 200.0f, -4.0f,
			TEXT("Pond in the farmland"));
		Add(TEXT("RiverValley"), 30000.0f, 15000.0f, 2.0f, 20.0f, -2.0f,
			TEXT("River valley on the mountain road"));
		Add(TEXT("MountainRoad"), 62000.0f, 12000.0f, 2.0f, 15.0f, 2.0f,
			TEXT("High on the mountain road"));
		Add(TEXT("Tropics"), -48000.0f, 2000.0f, 2.0f, 200.0f, -2.0f,
			TEXT("Southern tropical lowland"));
		Add(TEXT("Lagoon"), -64000.0f, 6000.0f, 2.0f, 75.0f, -3.0f,
			TEXT("Lagoon in the south"));
		Add(TEXT("Overlook"), 34000.0f, 6000.0f, 120.0f, 150.0f, -16.0f,
			TEXT("High overlook back toward the town and coast"));
		Add(TEXT("Vista"), 70000.0f, 20000.0f, 320.0f, 200.0f, -22.0f,
			TEXT("Mountain vista over the whole region"));
		// Newhaven, on the southern coastal shelf.
		// On the centre avenue, not on a block. The capture traces straight down
		// for the ground, so a point inside a block lands on a roof.
		Add(TEXT("Newhaven"), -123088.0f, 5153.0f, 1.8f, 99.0f, 3.0f,
			TEXT("Downtown Newhaven looking up the avenue"));
		Add(TEXT("NewhavenSkyline"), -162000.0f, -14000.0f, 70.0f, 40.0f, -3.0f,
			TEXT("The Newhaven skyline from the southern approach"));
		Add(TEXT("NewhavenWharf"), -125000.0f, 44000.0f, 4.0f, -90.0f, -2.0f,
			TEXT("The container wharf looking back at the city"));
		// The civic plaza: city hall behind the fountain. Worth its own
		// viewpoint because it is where the city's street life collects, and
		// because it spent a while being an empty rectangle - see the note on
		// check=False in city._place_plaza.
		// On the far side of the civic square, looking across the fountain to
		// the city hall on the block opposite. Note the height: the capture
		// traces down for the ground, so a point inside a building's footprint
		// puts the camera on its roof - which is where this viewpoint spent its
		// first attempt.
		Add(TEXT("NewhavenPlaza"), -128155.0f, 20238.0f, 1.8f, -81.0f, -1.0f,
			TEXT("Newhaven's civic square, looking across the fountain to the city hall"));
		Add(TEXT("NewhavenAerial"), -120220.0f, -12617.0f, 260.0f, 99.0f, -30.0f,
			TEXT("Newhaven street grid from above"));

		// Inside the houses. Eye height above the floor, so these frame what a
		// player sees on walking through a front door.
		AddIndoor(TEXT("HouseInterior"), 3319.1f, -585.5f, 1749.3f, 179.5f, -3.0f,
			TEXT("Inside a two storey town house, looking in from the front door"));
		AddIndoor(TEXT("HouseUpstairs"), 3299.1f, -585.3f, 2069.3f, 179.5f, -3.0f,
			TEXT("The bedroom floor of the same house"));
		AddIndoor(TEXT("CottageInterior"), 4615.5f, 3536.1f, 1737.9f, 2.1f, -3.0f,
			TEXT("Inside a one room cottage"));
		AddIndoor(TEXT("HouseHearth"), 1515.0f, -2056.3f, 1747.7f, -89.9f, -3.0f,
			TEXT("Looking back at the front door and windows from the far wall"));

		// Inside Newhaven. Every ground floor in the city is a business now,
		// so these are five different trades in five different archetypes.
		AddIndoor(TEXT("NewhavenShop"), -128884.6f, -3987.9f, 1210.6f, -80.8f, -4.0f,
			TEXT("A Newhaven grocery, from behind the counter"));
		AddIndoor(TEXT("NewhavenBarber"), -140842.0f, 4888.0f, 1214.4f, 98.8f, -4.0f,
			TEXT("A barber shop on the outer ring"));
		AddIndoor(TEXT("NewhavenSurgery"), -124467.3f, 5523.6f, 1244.1f, -81.0f, -4.0f,
			TEXT("A dental surgery on an office block ground floor"));
		AddIndoor(TEXT("NewhavenLobby"), -124749.5f, 9426.7f, 1255.0f, 9.3f, -4.0f,
			TEXT("The lobby of a downtown tower"));
		AddIndoor(TEXT("NewhavenDiner"), -124372.1f, -1054.4f, 1225.3f, 99.7f, -4.0f,
			TEXT("A restaurant off the avenue"));

		// The floors above the ground one, and the roof they lead to.
		AddIndoor(TEXT("NewhavenUpperFloor"), -119294.0f, 25982.3f, 3048.3f, -80.5f, -4.0f,
			TEXT("The fifth floor of a Newhaven office block"));
		AddIndoor(TEXT("NewhavenRoof"), -137299.3f, 4588.4f, 4842.2f, -80.7f, -6.0f,
			TEXT("On the roof of a Newhaven block, looking across the city"));
		// Fairhaven's church and its high street.
		AddIndoor(TEXT("FairhavenChurch"), -5200.0f, 2700.0f, 1721.8f, 90.0f, -4.0f,
			TEXT("Inside Fairhaven church, looking the length of the nave"));
		AddIndoor(TEXT("FairhavenShop"), 424.0f, -3762.0f, 1740.9f, 90.0f, -4.0f,
			TEXT("The grocery on Fairhaven high street"));

		// Development-only: the asset showcase grid (see Tools/Python/uegt2/showcase.py).
		Add(TEXT("ShowcaseA"), 27200.0f, -57500.0f, 3.4f, 90.0f, -3.0f,
			TEXT("Asset showcase, front row"));
		Add(TEXT("ShowcaseB"), 27200.0f, -59500.0f, 16.0f, 90.0f, -13.0f,
			TEXT("Asset showcase, raised"));
		// The valley overview faces away from the lower road crossing. Keep close
		// views of both approaches and the water clearance (world seed 20260826).
		Add(TEXT("BridgeSouth"), 20720.0f, 14960.0f, 1.7f, 16.26f, 1.5f,
			TEXT("Lower river bridge from the town-side approach"));
		Add(TEXT("BridgeNorth"), 26096.0f, 16528.0f, 1.7f, 196.26f, 1.5f,
			TEXT("Lower river bridge from the mountain-side approach"));
		Add(TEXT("BridgeSide"), 22148.0f, 20064.0f, 35.0f, -73.74f, -36.0f,
			TEXT("Lower river crossing, deck and both banks from the side"));
		Add(TEXT("TownCrossroads"), 4000.0f, -5300.0f, 18.0f, 90.0f, -25.0f,
			TEXT("High street junction where a shop formerly blocked the crossing road"));
		return Result;
	}();
	return Points;
}

float UUEGT2CaptureSubsystem::GroundHeightAt(float WorldX, float WorldY) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	FHitResult Hit;
	const FVector Start(WorldX, WorldY, 120000.0f);
	const FVector End(WorldX, WorldY, -60000.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2CaptureGround), true);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return static_cast<float>(Hit.ImpactPoint.Z);
	}
	// Fall back to sea level so a missing landscape still produces a frame.
	return 0.0f;
}

void UUEGT2CaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.WorldType != EWorldType::Game)
	{
		return;
	}

	if (IsFlySoakRequested())
	{
		const float SoakDelay = GetFloatArg(TEXT("UEGT2CaptureDelay="), 8.0f);
		InWorld.GetTimerManager().SetTimer(TimerHandle,
			FTimerDelegate::CreateUObject(this, &UUEGT2CaptureSubsystem::BeginFlySoak),
			FMath::Max(SoakDelay, 0.5f), false);
		return;
	}

	if (IsWalkSmokeRequested())
	{
		const float SmokeDelay = GetFloatArg(TEXT("UEGT2CaptureDelay="), 6.0f);
		InWorld.GetTimerManager().SetTimer(TimerHandle,
			FTimerDelegate::CreateUObject(this, &UUEGT2CaptureSubsystem::BeginWalkSmoke),
			FMath::Max(SmokeDelay, 0.5f), false);
		return;
	}

	if (!IsCaptureRequested())
	{
		return;
	}

	OutputDirectory = GetStringArg(CaptureSwitch, FPaths::ProjectSavedDir() / TEXT("Screenshots"));
	HoldSeconds = GetFloatArg(TEXT("UEGT2CaptureHold="), 1.6f);
	const float Delay = GetFloatArg(TEXT("UEGT2CaptureDelay="), 6.0f);

	const FString Only = GetStringArg(TEXT("UEGT2CaptureOnly="), FString());
	Tour = GetTour();
	if (!Only.IsEmpty())
	{
		Tour = Tour.FilterByPredicate([&Only](const FUEGT2Viewpoint& Point)
		{
			return Point.Name.ToString().Equals(Only, ESearchCase::IgnoreCase);
		});
	}

	UE_LOG(LogUEGT2Diag, Log,
		TEXT("Capture tour requested: %d viewpoints -> %s (delay %.1fs, hold %.1fs)"),
		Tour.Num(), *OutputDirectory, Delay, HoldSeconds);

	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(
		this, &UUEGT2CaptureSubsystem::HandleScreenshotCaptured);

	bMenuMode = FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureMenu"));
	bDialogueMode = FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureDialogue"));
	bLifeMode = IsLifeCaptureRequested();
	bSquareWashrooms = FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureSquareWashrooms"));

	// Give Lumen, virtual shadow maps and streaming time to settle first.
	InWorld.GetTimerManager().SetTimer(TimerHandle,
		FTimerDelegate::CreateUObject(this, bLifeMode
			? &UUEGT2CaptureSubsystem::BeginLifeTour
			: bDialogueMode
				? &UUEGT2CaptureSubsystem::BeginDialogueTour
				: bMenuMode
					? &UUEGT2CaptureSubsystem::BeginMenuTour
					: &UUEGT2CaptureSubsystem::BeginTour),
		FMath::Max(Delay, 0.5f), false);
}

void UUEGT2CaptureSubsystem::BeginTour()
{
	UWorld* World = GetWorld();
	if (!World || Tour.Num() == 0)
	{
		FinishTour();
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Capture tour: no player controller."));
		FinishTour();
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TourCamera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TourCamera)
	{
		FinishTour();
		return;
	}
	if (UCameraComponent* Component = TourCamera->GetCameraComponent())
	{
		Component->SetFieldOfView(88.0f);
	}
	PC->SetViewTarget(TourCamera);

	Index = 0;
	CaptureNext();
}

void UUEGT2CaptureSubsystem::CaptureNext()
{
	UWorld* World = GetWorld();
	if (!World || !Tour.IsValidIndex(Index) || !TourCamera)
	{
		FinishTour();
		return;
	}

	const FUEGT2Viewpoint& Point = Tour[Index];
	const float Z = Point.bAbsoluteHeight
		? Point.HeightAboveGround
		: GroundHeightAt(Point.Location.X, Point.Location.Y)
			+ Point.HeightAboveGround * 100.0f;
	const FVector Location(Point.Location.X, Point.Location.Y, Z);
	TourCamera->SetActorLocationAndRotation(Location, FRotator(Point.Pitch, Point.Yaw, 0.0f));

	// Move first, then wait a beat so temporal effects resolve before the shot.
	World->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda(
		[this, Point, Location]()
		{
			const FString FileName = FString::Printf(TEXT("%s/%02d_%s.png"),
				*OutputDirectory, Index + 1, *Point.Name.ToString());

			UE_LOG(LogUEGT2Diag, Log,
				TEXT("Capture %02d/%02d '%s' at (%.0f, %.0f, %.0f): %s"),
				Index + 1, Tour.Num(), *Point.Name.ToString(),
				Location.X, Location.Y, Location.Z, *Point.Description);

			// Ask for a raw capture and write the PNG ourselves: the built-in
			// filename path does not produce a file in an offscreen game build.
			PendingFileName = FileName;
			FScreenshotRequest::RequestScreenshot(false);

			// Give the screenshot a frame to be written, then advance.
			if (UWorld* Inner = GetWorld())
			{
				Inner->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda(
					[this]()
					{
						++Index;
						CaptureNext();
					}), 0.6f, false);
			}
		}), HoldSeconds, false);
}


// ---------------------------------------------------------------------------
// Dialogue capture: proves the conversation panel renders, with a real
// inhabitant's real state in it, without a human standing in front of one.
// ---------------------------------------------------------------------------
void UUEGT2CaptureSubsystem::BeginDialogueTour()
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	if (!PC)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Dialogue capture: no UEGT2 player controller."));
		FinishTour();
		return;
	}

	// Whoever is nearest the player start and can actually be spoken to. Taking
	// the nearest rather than a named one keeps this working when the seed
	// moves the population around.
	AUEGT2NPCActor* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	const FVector From = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	for (TActorIterator<AUEGT2NPCActor> It(World); It; ++It)
	{
		AUEGT2NPCActor* NPC = *It;
		if (!NPC || NPC->IsAnimal() || !NPC->CanInteract(PC->GetPawn()))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(From, NPC->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = NPC;
		}
	}

	if (!Best)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Dialogue capture: nobody to talk to."));
		FinishTour();
		return;
	}

	// Stand the player in front of them and look at them, so the shot shows a
	// conversation and not a panel floating over empty grass.
	if (APawn* Pawn = PC->GetPawn())
	{
		const FVector Target = Best->GetActorLocation();
		const FVector Offset = FVector(210.0f, 60.0f, 0.0f);
		Pawn->TeleportTo(Target + Offset + FVector(0.0f, 0.0f, 40.0f),
			Pawn->GetActorRotation(), false, true);
		PC->SetControlRotation((Target - (Target + Offset)).Rotation());
	}

	DialoguePartner = Best;
	PC->OpenDialogue(Best);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Dialogue capture: talking to %s."),
		*Best->GetDisplayName().ToString());
	RunDialogueStep();
}

void UUEGT2CaptureSubsystem::RunDialogueStep()
{
	static const TCHAR* Names[] = { TEXT("Opening"), TEXT("Asked") };
	if (DialogueIndex >= (int32)UE_ARRAY_COUNT(Names))
	{
		FinishTour();
		return;
	}

	// The second shot has the transcript filled in, so the review shows what
	// the panel looks like in use rather than only at its emptiest.
	if (DialogueIndex == 1)
	{
		if (AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(
			UGameplayStatics::GetPlayerController(GetWorld(), 0)))
		{
			PC->AskDialogueTopic((int32)EUEGT2DialogueTopic::Wellbeing);
			PC->AskDialogueTopic((int32)EUEGT2DialogueTopic::Doing);
			PC->AskDialogueTopic((int32)EUEGT2DialogueTopic::Follow);
		}
	}

	const FString Name = Names[DialogueIndex];
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, Name](float) -> bool
		{
			PendingFileName = FString::Printf(TEXT("%s/talk_%02d_%s.png"),
				*OutputDirectory, DialogueIndex + 1, *Name);
			UE_LOG(LogUEGT2Diag, Log, TEXT("Dialogue capture %02d: %s"),
				DialogueIndex + 1, *Name);
			FScreenshotRequest::RequestScreenshot(true);

			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float) -> bool
				{
					++DialogueIndex;
					RunDialogueStep();
					return false;
				}), 0.9f);
			return false;
		}), HoldSeconds);
}

// ---------------------------------------------------------------------------
// Fly soak: reproduce the god-mode freeze and say what it was.
// ---------------------------------------------------------------------------
void UUEGT2CaptureSubsystem::BeginFlySoak()
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	UUEGT2DevModeSubsystem* Dev = UUEGT2DevModeSubsystem::Get(World);
	if (!PC || !Explorer || !Dev)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_FLY_SOAK_FAILED: no player or dev subsystem."));
		FinishTour();
		return;
	}

	PC->CloseMenu();
	const float RequestedMinutes = GetFloatArg(TEXT("UEGT2SmokeMinutes="), 6.0f);
	FlyLimitSeconds = RequestedMinutes * 60.0f;
	if (!FMath::IsFinite(RequestedMinutes) || RequestedMinutes <= 0.0f ||
		!FMath::IsFinite(FlyLimitSeconds) || FlyLimitSeconds <= 0.0f)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_FLY_SOAK_FAILED: SmokeMinutes must be finite and positive (got %.9g)."), RequestedMinutes);
		FinishTour();
		return;
	}

	// Exactly what a person does: dev mode, god, fly, and wind the speed up.
	Dev->SetDevModeEnabled(true);
	Dev->SetGodMode(true);
	Dev->SetFlyEnabled(true);
	Dev->SetSpeedMultiplier(GetFloatArg(TEXT("UEGT2SmokeSpeed="), 8.0f));
	// Noclip unless told otherwise. Flying with collision on wedges the pawn
	// against the first Newhaven tower it meets, and a soak that spends six of
	// its seven minutes hovering inside an office block measures nothing.
	if (!FParse::Param(FCommandLine::Get(), TEXT("UEGT2SmokeCollide")))
	{
		Dev->SetNoclipEnabled(true);
	}

	// Time a full purge from the inside. A level this size has hundreds of
	// thousands of objects in it, and a collection that walks all of them is
	// indistinguishable from a freeze at the keyboard.
	FlyGcPreHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda(
		[this]() { FlyGcStartSeconds = FPlatformTime::Seconds(); });
	FlyGcPostHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda(
		[this]()
		{
			const float Took = (float)((FPlatformTime::Seconds() - FlyGcStartSeconds) * 1000.0);
			FlyGcLastMs = FMath::Max(FlyGcLastMs, Took);
			FlyGcWorstMs = FMath::Max(FlyGcWorstMs, Took);
			++FlyGcCount;
		});

	// The inhabited half of the map: both settlements, the market, the quays and
	// the farms, in an order that keeps crossing between empty ground and a
	// crowd. Read off the tour so it follows the world if the seed moves it.
	FlyStops.Reset();
	static const TCHAR* Circuit[] = {
		TEXT("TownSquare"), TEXT("Market"), TEXT("Waterfront"), TEXT("Farmland"),
		TEXT("NewhavenPlaza"), TEXT("Newhaven"), TEXT("NewhavenWharf"),
		TEXT("MainStreet") };
	for (const TCHAR* Name : Circuit)
	{
		for (const FUEGT2Viewpoint& Point : GetTour())
		{
			if (Point.Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				FlyStops.Add(Point.Location);
				break;
			}
		}
	}
	if (FlyStops.Num() == 0)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_FLY_SOAK_FAILED: no viewpoints to circuit."));
		FinishTour();
		return;
	}
	// Above the tallest Newhaven tower, so the circuit crosses the city rather
	// than through it.
	FlyAltitude = GetFloatArg(TEXT("UEGT2SmokeAltitude="), 4000.0f);
	FlyStop = 0;
	FlyLaps = 0;
	FlyStuckSeconds = 0.0f;
	FlyLastCheck = Explorer->GetActorLocation();

	FlyElapsed = 0.0f;
	FlySecondElapsed = 0.0f;
	FlySecondFrames = 0;
	FlySecondWorst = 0.0f;
	FlyWorstEver = 0.0f;
	FlyStalls = 0;
	FlyGcCount = 0;

	UE_LOG(LogUEGT2Diag, Log,
		TEXT("UEGT2_FLY_SOAK_START %.0f s, speed %.0fx, from %s"),
		FlyLimitSeconds, Dev->GetSpeedMultiplier(),
		*Explorer->GetActorLocation().ToCompactString());

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(
		this, &UUEGT2CaptureSubsystem::TickFlySoak), 0.0f);
}

void UUEGT2CaptureSubsystem::ReportFlySecond()
{
	UWorld* World = GetWorld();
	const APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();

	const float MeanMs = FlySecondFrames > 0
		? (FlySecondElapsed * 1000.0f / FlySecondFrames) : 0.0f;
	// Anything past this is visible as a stutter; past a second it is a freeze.
	const bool bStall = FlySecondWorst > 250.0f;
	if (bStall)
	{
		++FlyStalls;
	}

	UE_LOG(LogUEGT2Diag, Log,
		TEXT("%s t=%5.0fs frames=%3d mean=%6.1fms worst=%8.1fms  gc=%d last=%7.1fms  ")
		TEXT("objects=%7d  used=%5llu MB  hour=%5.2f near=%4d out=%4d  leg=%d  at %s"),
		bStall ? TEXT("UEGT2_FLY_STALL") : TEXT("UEGT2_FLY      "),
		FlyElapsed, FlySecondFrames, MeanMs, FlySecondWorst,
		FlyGcCount, FlyGcLastMs,
		GUObjectArray.GetObjectArrayNumMinusAvailable(),
		(uint64)(Memory.UsedPhysical / (1024 * 1024)),
		Director ? Director->GetHour() : -1.0f,
		Director ? Director->GetNearCount() : -1,
		Director ? Director->GetActiveCount() : -1, FlyStop,
		Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("nowhere"));

	FlySecondElapsed = 0.0f;
	FlySecondFrames = 0;
	FlySecondWorst = 0.0f;
	FlyGcLastMs = 0.0f;
}

bool UUEGT2CaptureSubsystem::TickFlySoak(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	UUEGT2InputConfig* Config = PC ? PC->GetInputConfig() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = PC
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;

	if (!PC || !Explorer || !Config || !Config->MoveAction || !Input)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_FLY_SOAK_FAILED: input unavailable."));
		FinishTour();
		return false;
	}

	// A circuit of the inhabited places, not a straight line. Flying forward on
	// a slow turn sounds like a fair soak and is not: it leaves the map, climbs
	// into empty sky and never sees an inhabitant again, which tests nothing.
	// Where the people are is the whole point - the tiers, the repaths and the
	// crowd only happen where somebody lives.
	if (!FlyStops.IsValidIndex(FlyStop))
	{
		FlyStop = 0;
	}
	const FVector2D Target = FlyStops[FlyStop];
	const float TargetZ = GroundHeightAt(Target.X, Target.Y) + FlyAltitude;
	const FVector Here = Explorer->GetActorLocation();
	const FVector To(Target.X - Here.X, Target.Y - Here.Y, TargetZ - Here.Z);

	if (To.Size2D() < 6000.0f)
	{
		FlyStop = (FlyStop + 1) % FlyStops.Num();
		++FlyLaps;
	}

	// Stuck detector. Even with noclip the pawn can end up going nowhere, and
	// the failure mode is silent: the log fills with identical lines and reads
	// like a clean flight. Move on rather than measure a hover.
	if (FVector::Dist2D(Here, FlyLastCheck) < 500.0f)
	{
		FlyStuckSeconds += DeltaSeconds;
		if (FlyStuckSeconds > 3.0f)
		{
			UE_LOG(LogUEGT2Diag, Warning,
				TEXT("UEGT2_FLY stuck at %s heading for leg %d; skipping it."),
				*Here.ToCompactString(), FlyStop);
			FlyStop = (FlyStop + 1) % FlyStops.Num();
			FlyStuckSeconds = 0.0f;
			FlyLastCheck = Here;
		}
	}
	else
	{
		FlyStuckSeconds = 0.0f;
		FlyLastCheck = Here;
	}

	static const TArray<UInputModifier*> NoModifiers;
	static const TArray<UInputTrigger*> NoTriggers;
	Input->InjectInputForAction(Config->MoveAction,
		FInputActionValue(FVector2D(0.0f, 1.0f)), NoModifiers, NoTriggers);
	// Flight follows the full control rotation, so steering is just looking at
	// where you want to go.
	PC->SetControlRotation(To.Rotation());

	FlyElapsed += DeltaSeconds;
	FlySecondElapsed += DeltaSeconds;
	++FlySecondFrames;
	FlySecondWorst = FMath::Max(FlySecondWorst, DeltaSeconds * 1000.0f);
	FlyWorstEver = FMath::Max(FlyWorstEver, DeltaSeconds * 1000.0f);

	if (FlySecondElapsed >= 1.0f)
	{
		ReportFlySecond();
	}

	if (FlyElapsed < FlyLimitSeconds)
	{
		return true;
	}

	UE_LOG(LogUEGT2Diag, Log,
		TEXT("UEGT2_FLY_SOAK_COMPLETE ran=%.0fs stalls=%d worst=%.1fms collections=%d "
			 "worstgc=%.1fms legs=%d"),
		FlyElapsed, FlyStalls, FlyWorstEver, FlyGcCount, FlyGcWorstMs, FlyLaps);

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(FlyGcPreHandle);
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(FlyGcPostHandle);
	FinishTour();
	return false;
}

// ---------------------------------------------------------------------------
// Amenity capture: proves the player can actually live here.
// ---------------------------------------------------------------------------
namespace UEGT2Capture
{
	/** The kinds worth photographing, and the order to visit them in. */
	const EUEGT2AmenityKind LifeKinds[] = {
		EUEGT2AmenityKind::Work,      // earn it first, which is rather the point
		EUEGT2AmenityKind::Food,
		EUEGT2AmenityKind::Washroom,
		EUEGT2AmenityKind::Seat,
		EUEGT2AmenityKind::Tavern,
		EUEGT2AmenityKind::Bed,
	};

	/**
	 * World hours charged at each stop.
	 *
	 * A fixed slice rather than elapsed time, because the clock is frozen
	 * during a capture and because a screenshot that depends on how long the
	 * machine took to render the last one is not one you can compare against
	 * last week's.
	 */
	constexpr float LifeHoursPerStop = 0.55f;
}

void UUEGT2CaptureSubsystem::BeginLifeTour()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	UUEGT2NeedsComponent* Life = Explorer ? Explorer->GetLife() : nullptr;
	if (!Life)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Amenity capture: no player with needs."));
		FinishTour();
		return;
	}

	if (bSquareWashrooms)
	{
		// Cooked tags identify the four required square props even after a seed
		// change. Their local -Y doors and the shared NPC survey define the venue.
		for (int32 Stop = 0; Stop < 4; ++Stop)
		{
			const FName Tag(*FString::Printf(TEXT("UEGT2.SquarePrivy.%d"), Stop));
			AActor* Privy = nullptr;
			int32 PropCount = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(Tag)) { Privy = *It; ++PropCount; }
			}
			if (PropCount != 1)
			{
				UE_LOG(LogUEGT2Diag, Error, TEXT("Square washroom capture: %s has %d props."), *Tag.ToString(), PropCount);
				FinishTour(); return;
			}
			const FVector Front = -Privy->GetActorRightVector();
			const FVector Venue = Privy->GetActorLocation() + Front * 225.0;
			AUEGT2Amenity* Match = nullptr;
			int32 VenueCount = 0;
			for (TActorIterator<AUEGT2Amenity> It(World); It; ++It)
			{
				if (It->GetKind() == EUEGT2AmenityKind::Washroom && It->GetActorLocation().Equals(Venue, 1.0))
				{
					Match = *It; ++VenueCount;
				}
			}
			if (VenueCount != 1)
			{
				UE_LOG(LogUEGT2Diag, Error, TEXT("Square washroom capture: %s has %d front amenities."), *Tag.ToString(), VenueCount);
				FinishTour(); return;
			}
			// The 350cm approach reservation has a 120cm radius. This pose still
			// fits the normal capsule inside it, with room to frame the doorstep.
			const FVector Approach = Privy->GetActorLocation() + Front * 425.0;
			FHitResult Ground;
			const FCollisionQueryParams Params(SCENE_QUERY_STAT(SquareWashroomCapture), false, Explorer);
			const UCapsuleComponent* Capsule = Explorer->GetCapsuleComponent();
			const float Half = Capsule->GetScaledCapsuleHalfHeight();
			if (!World->LineTraceSingleByObjectType(Ground, Approach + FVector(0, 0, 120), Approach - FVector(0, 0, 120),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params) || !Explorer->GetCharacterMovement()->IsWalkable(Ground))
			{
				UE_LOG(LogUEGT2Diag, Error, TEXT("Square washroom capture: %s approach has no walkable ground."), *Tag.ToString());
				FinishTour(); return;
			}
			const FVector Stand = Ground.ImpactPoint + FVector(0, 0, Half + 2.4);
			if (World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Half), Params))
			{
				UE_LOG(LogUEGT2Diag, Error, TEXT("Square washroom capture: %s approach blocks the player capsule."), *Tag.ToString());
				FinishTour(); return;
			}
			LifeStops.Add(Match);
			LifeStands.Add(Stand);
			UE_LOG(LogUEGT2Diag, Log, TEXT("Square washroom capture: %s stand=%s amenity=%s."),
				*Tag.ToString(), *Stand.ToCompactString(), *Match->GetActorLocation().ToCompactString());
		}
	}
	else
	{
		// The nearest one of each kind to where the player woke up. Nearest rather
		// than named, so re-rolling the seed does not break the tour.
		const FVector From = Explorer->GetActorLocation();
		for (EUEGT2AmenityKind Kind : UEGT2Capture::LifeKinds)
		{
			AUEGT2Amenity* Best = nullptr;
			double BestDistance = TNumericLimits<double>::Max();
			for (TActorIterator<AUEGT2Amenity> It(World); It; ++It)
			{
				AUEGT2Amenity* Amenity = *It;
				if (!Amenity || Amenity->GetKind() != Kind)
				{
					continue;
				}
				const double Distance = FVector::DistSquared(From, Amenity->GetActorLocation());
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Amenity;
				}
			}
			if (Best)
			{
				LifeStops.Add(Best);
			}
			else
			{
				UE_LOG(LogUEGT2Diag, Warning, TEXT("Amenity capture: no %s anywhere in the world."),
					UEGT2AmenityKindName(Kind));
			}
		}
	}

	// Start hungry, tired and caught short, so each stop has something visible
	// to do. A full set of bars proves nothing.
	Life->SetNeedsSatisfied(false);
	Life->SetCoins(40.0f);

	LifeIndex = 0;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Amenity capture: %d stops -> %s"),
		LifeStops.Num(), *OutputDirectory);
	RunLifeStep();
}

void UUEGT2CaptureSubsystem::RunLifeStep()
{
	UWorld* World = GetWorld();
	if (!World || !LifeStops.IsValidIndex(LifeIndex))
	{
		FinishTour();
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	AUEGT2Amenity* Amenity = LifeStops[LifeIndex];
	UUEGT2NeedsComponent* Life = Explorer ? Explorer->GetLife() : nullptr;
	if (!Life || !Amenity)
	{
		FinishTour();
		return;
	}

	// Stand a stride back from it and look at it, so the interaction probe -
	// the real one, sweeping on Visibility from the real camera - has to find
	// it exactly as it would if somebody had walked up.
	//
	// Which side to stand on is not a free choice. A bench has a 155 cm
	// backrest and a privy is a shed: from the wrong side the thing itself is
	// between the camera and the volume, the probe stops at the first blocking
	// hit, and the tour reports a working amenity as broken. So try eight
	// approaches and take the first with a clear line - which is what a person
	// walking up to a bench does without thinking about it.
	const FVector Point = Amenity->GetInteractionPoint();
	const FVector Ground = Amenity->GetActorLocation();
	const float Reach = 175.0f;      // inside the 320 cm probe and every use range

	FVector Stand = Point + FVector(Reach, 0.0f, 0.0f);
	for (int32 Step = 0; !bSquareWashrooms && Step < 8; ++Step)
	{
		const float Angle = FMath::DegreesToRadians(Step * 45.0f);
		const FVector Candidate(Point.X + FMath::Cos(Angle) * Reach,
			Point.Y + FMath::Sin(Angle) * Reach, Ground.Z + 95.0f);
		// Eye height, so this is the line the probe will actually sweep.
		const FVector Eye = Candidate + FVector(0.0f, 0.0f, 68.0f);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2CaptureLife), false, Explorer);
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit, Eye, Point, ECC_Visibility, Params) && Hit.GetActor() != Amenity;
		if (!bBlocked)
		{
			Stand = Candidate;
			break;
		}
	}

	if (bSquareWashrooms)
	{
		Stand = LifeStands[LifeIndex];
		if (!Explorer->TeleportTo(Stand, Explorer->GetActorRotation(), false, false)
			|| !Explorer->GetActorLocation().Equals(Stand, 1.0))
		{
			UE_LOG(LogUEGT2Diag, Error, TEXT("Square washroom capture: stop %d rejected its validated standing position."), LifeIndex);
			FinishTour(); return;
		}
		Life->SetNeedsSatisfied(false);
	}
	else
	{
		Explorer->TeleportTo(Stand, Explorer->GetActorRotation(), false, true);
	}
	// A level view through the tall washroom volume keeps both the doorstep
	// and roof in frame; looking down at its centre crops the roof at this range.
	const FVector Aim = bSquareWashrooms ? FVector(Point.X, Point.Y, Explorer->GetPawnViewLocation().Z) : Point;
	PC->SetControlRotation((Aim - Explorer->GetPawnViewLocation()).Rotation());

	const FUEGT2NPCNeeds Before = Life->GetNeeds();
	const int32 CoinsBefore = Life->GetCoins();

	// A beat for the probe to notice what is in front of it, then use it.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, Amenity, Life, Explorer, Before, CoinsBefore](float) -> bool
		{
			UUEGT2InteractionComponent* Probe = Explorer->GetInteraction();
			const bool bFound = Probe && Probe->GetFocusedActor() == Amenity;
			const bool bUsed = bFound && Probe->TryInteract();

			// A fixed slice of world time, charged through the same function
			// the whole town runs on.
			Life->AdvanceLife(UEGT2Capture::LifeHoursPerStop);

			const FUEGT2NPCNeeds& After = Life->GetNeeds();
			if (bSquareWashrooms && (!bUsed || Life->GetActivity() != EUEGT2Activity::Washroom || After.Relief <= Before.Relief))
			{
				UE_LOG(LogUEGT2Diag, Error, TEXT("Square washroom capture: stop %d did not use the washroom and improve relief."), LifeIndex);
				FinishTour(); return false;
			}
			UE_LOG(LogUEGT2Diag, Log,
				TEXT("Amenity capture %02d: %s '%s' - %s, %s. %s. ")
				TEXT("fed %.2f->%.2f energy %.2f->%.2f relief %.2f->%.2f coins %d->%d"),
				LifeIndex + 1, UEGT2AmenityKindName(Amenity->GetKind()),
				*Amenity->GetVenueName().ToString(),
				bFound ? TEXT("found by the probe") : TEXT("NOT FOUND BY THE PROBE"),
				bUsed ? TEXT("used") : TEXT("NOT USED"),
				*Life->GetActivityText().ToString(),
				Before.Fed, After.Fed, Before.Energy, After.Energy,
				Before.Relief, After.Relief, CoinsBefore, Life->GetCoins());

			PendingFileName = FString::Printf(TEXT("%s/life_%02d_%s.png"),
				*OutputDirectory, LifeIndex + 1,
				UEGT2AmenityKindName(Amenity->GetKind()));
			FScreenshotRequest::RequestScreenshot(true);

			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float) -> bool
				{
					++LifeIndex;
					RunLifeStep();
					return false;
				}), 0.9f);
			return false;
		}), FMath::Max(HoldSeconds, 0.4f));
}

// ---------------------------------------------------------------------------
// Menu capture: proves the front end, every settings tab and the pause screen
// actually render, without a human opening the game.
// ---------------------------------------------------------------------------
namespace
{
	struct FMenuShot { const TCHAR* Name; int32 SettingsTab; bool bPause; };

	const FMenuShot MenuShots[] = {
		{ TEXT("MainMenu"),        -1, false },
		{ TEXT("SettingsGraphics"), 0, false },
		{ TEXT("SettingsAudio"),    1, false },
		{ TEXT("SettingsControls"), 2, false },
		{ TEXT("SettingsGameplay"), 3, false },
		{ TEXT("PauseMenu"),       -1, true  },
	};
}

void UUEGT2CaptureSubsystem::BeginMenuTour()
{
	MenuIndex = 0;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Menu capture: %d screens -> %s"),
		(int32)UE_ARRAY_COUNT(MenuShots), *OutputDirectory);
	RunMenuStep();
}

void UUEGT2CaptureSubsystem::RunMenuStep()
{
	UWorld* World = GetWorld();
	if (!World || MenuIndex >= (int32)UE_ARRAY_COUNT(MenuShots))
	{
		FinishTour();
		return;
	}

	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(
		UGameplayStatics::GetPlayerController(World, 0));
	if (!PC)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Menu capture: no UEGT2 player controller."));
		FinishTour();
		return;
	}

	const FMenuShot& Shot = MenuShots[MenuIndex];
	if (Shot.bPause)
	{
		PC->CloseMenu();
		PC->ShowPauseMenu();
	}
	else if (Shot.SettingsTab >= 0)
	{
		PC->ShowSettingsPage(Shot.SettingsTab);
	}
	else
	{
		PC->ShowMainMenu();
	}

	// Real-time ticker, not the world timer: the pause screen actually pauses
	// the world, which would stop world timers and hang the capture.
	const FString Name = Shot.Name;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, Name](float) -> bool
		{
			PendingFileName = FString::Printf(TEXT("%s/menu_%02d_%s.png"),
				*OutputDirectory, MenuIndex + 1, *Name);
			UE_LOG(LogUEGT2Diag, Log, TEXT("Menu capture %02d: %s"), MenuIndex + 1, *Name);
			FScreenshotRequest::RequestScreenshot(true);

			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float) -> bool
				{
					++MenuIndex;
					RunMenuStep();
					return false;
				}), 0.9f);
			return false;
		}), FMath::Max(HoldSeconds, 0.8f));
}


// ---------------------------------------------------------------------------
// Walk smoke: injects real input through Enhanced Input and checks the player
// actually moved. This is what catches "the game looks fine and nothing is
// bound", which is exactly what a missing DefaultInputComponentClass causes.
// ---------------------------------------------------------------------------
void UUEGT2CaptureSubsystem::BeginWalkSmoke()
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;

	if (!PC || !Explorer)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SMOKE_WALK_FAILED: no player pawn."));
		FinishTour();
		return;
	}

	PC->CloseMenu();
	WalkStart = Explorer->GetActorLocation();
	WalkElapsed = 0.0f;

	UE_LOG(LogUEGT2Diag, Log, TEXT("Walk smoke starting at %s"),
		*WalkStart.ToCompactString());

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(
		this, &UUEGT2CaptureSubsystem::TickWalkSmoke), 0.0f);
}

bool UUEGT2CaptureSubsystem::TickWalkSmoke(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	UUEGT2InputConfig* Config = PC ? PC->GetInputConfig() : nullptr;

	if (!PC || !Explorer || !Config || !Config->MoveAction)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SMOKE_WALK_FAILED: input config unavailable."));
		FinishTour();
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* Input =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Input)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SMOKE_WALK_FAILED: no Enhanced Input subsystem."));
		FinishTour();
		return false;
	}

	// Straight forward, injected as an action value.
	//
	// Note what this does NOT cover: injecting the action skips the key
	// mapping and its modifiers entirely, so it cannot see a binding that
	// points the wrong way. S moved the player forward for a long time and this
	// test passed throughout. UEGT2.Player.MoveContext covers that layer by
	// building the real mapping context and running the real modifier chain.
	static const TArray<UInputModifier*> NoModifiers;
	static const TArray<UInputTrigger*> NoTriggers;
	Input->InjectInputForAction(Config->MoveAction,
		FInputActionValue(FVector2D(0.0f, 1.0f)), NoModifiers, NoTriggers);
	if (Config->SprintAction && WalkElapsed > 1.5f)
	{
		Input->InjectInputForAction(Config->SprintAction,
			FInputActionValue(true), NoModifiers, NoTriggers);
	}

	WalkElapsed += DeltaSeconds;
	if (WalkElapsed < 4.0f)
	{
		return true;
	}

	const FVector End = Explorer->GetActorLocation();
	const float Distance = FVector::Dist2D(WalkStart, End);
	UE_LOG(LogUEGT2Diag, Log,
		TEXT("UEGT2_SMOKE_WALK_COMPLETE distance=%.0f start=%s end=%s speed=%.0f"),
		Distance, *WalkStart.ToCompactString(), *End.ToCompactString(),
		Explorer->GetHorizontalSpeed());

	if (Distance < 300.0f)
	{
		UE_LOG(LogUEGT2Diag, Error,
			TEXT("UEGT2_SMOKE_WALK_FAILED: moved only %.0f cm in 4s; input is not reaching the pawn."),
			Distance);
	}
	FinishTour();
	return false;
}

void UUEGT2CaptureSubsystem::FinishTour()
{
	UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_CAPTURE_TOUR_COMPLETE (%d viewpoints)"), Tour.Num());

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}
	// Real-time ticker so this still fires when the pause menu paused the world.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
	{
		FPlatformMisc::RequestExit(false);
		return false;
	}), 1.6f);
}

void UUEGT2CaptureSubsystem::HandleScreenshotCaptured(int32 Width, int32 Height,
	const TArray<FColor>& Bitmap)
{
	if (PendingFileName.IsEmpty() || Width <= 0 || Height <= 0)
	{
		return;
	}
	const FString FileName = PendingFileName;
	PendingFileName.Reset();

	// Offscreen captures come back with zero alpha; force it opaque.
	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque)
	{
		Pixel.A = 255;
	}

	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (FFileHelper::SaveArrayToFile(Png, *FileName))
	{
		UE_LOG(LogUEGT2Diag, Log, TEXT("Wrote %dx%d screenshot: %s"), Width, Height, *FileName);
	}
	else
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Failed to write screenshot: %s"), *FileName);
	}
}
