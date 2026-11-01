#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"

struct FSlateBrush;

namespace BallsStyle {

inline constexpr float StrongTextMinimumSize = 24.0f;

FSlateFontInfo Font(float Size, bool bBold = false);
const FButtonStyle& Button();

const FButtonStyle& SelectedButton();

const FButtonStyle& HeaderButton();

const FButtonStyle& DropdownButton();

const FSlateBrush* RoundedBox(float Radius);
const FSlateBrush* Panel();
const FSlateBrush* White();

FVector2D Measure(const FString& Text, const FSlateFontInfo& FontInfo);

}
