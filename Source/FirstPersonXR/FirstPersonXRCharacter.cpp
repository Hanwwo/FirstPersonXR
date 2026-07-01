// Copyright Epic Games, Inc. All Rights Reserved.

#include "FirstPersonXRCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FirstPersonXR.h"


DEFINE_LOG_CATEGORY(LogTemplateCharacter);


AFirstPersonXRCharacter::AFirstPersonXRCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
}

void AFirstPersonXRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AFirstPersonXRCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AFirstPersonXRCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFirstPersonXRCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFirstPersonXRCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFirstPersonXRCharacter::LookInput);

		// InteractAction 신호가 '시작(Started)'되면, 나의(this) Interact 함수를 실행
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AFirstPersonXRCharacter::Interact);
	}
	else
	{
		UE_LOG(LogFirstPersonXR, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void AFirstPersonXRCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AFirstPersonXRCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AFirstPersonXRCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AFirstPersonXRCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);     // vector와 scale
		AddMovementInput(GetActorForwardVector(), Forward);  //right와 forward는 -1.0~1.0(보통)
	}
}

void AFirstPersonXRCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AFirstPersonXRCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

void AFirstPersonXRCharacter::Interact()
{
	// TODO: LineTrace(레이캐스트) 사물 인식 로직
	// 1. 뇌(Controller)와 눈(Camera)이 월드에 존재하는지 안전성 검사
	if (GetController() == nullptr || FirstPersonCameraComponent == nullptr)
	{
		return;
	}

	// 2. 레이저의 시작점(Start) 계산: 현재 카메라의 월드 위치
	FVector TraceStart = FirstPersonCameraComponent->GetComponentLocation();

	// 3. 레이저의 발사 방향 계산: 현재 카메라가 조준하고 있는 정면 방향
	FVector ForwardVector = FirstPersonCameraComponent->GetForwardVector();

	// 4. 레이저의 사거리 설정 (단위: cm / 500cm = 5m)
	float TraceRange = 500.0f;

	// 5. 레이저의 끝점(End) 계산: 시작점 + (방향 * 사거리)
	FVector TraceEnd = TraceStart + (ForwardVector * TraceRange);

	// 6. 충돌 데이터를 담아올 빈 결과 상자 생성
	FHitResult HitResult;

	// 7. 레이저 발사 시 충돌 쿼리 설정 (플레이어 본인의 몸통은 레이저에 맞지 않도록 예외 처리)
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// 8. 엔진 월드에 레이저 투사 명령 위임 (시각적 디버그 선을 그리도록 스위치 ON)
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,			// 충돌 결과가 기록될 상자
		TraceStart,			// 시작점
		TraceEnd,			// 끝점
		ECC_Visibility,		// 충돌 감지 필터 채널 (눈에 보이는 모든 콜리전 대상)
		QueryParams			// 쿼리 조건 설정
	);

	// 9. 충돌 판정 검증 및 타깃 데이터 제어 (상태 변화)
	if (bHit && HitResult.GetActor())
	{
		// 9-1. 타깃 액터의 메모리 주소를 포인터 변수에 안전하게 할당
		AActor* HitActor = HitResult.GetActor();

		UE_LOG(LogTemplateCharacter, Log, TEXT("상호작용 타깃 확인 -> %s"), *HitActor->GetName());

		// 9-2. 타깃 액터의 상태 변경 (월드 공간에서 제거 및 메모리 해제 요청)
		HitActor->Destroy();
	}
}