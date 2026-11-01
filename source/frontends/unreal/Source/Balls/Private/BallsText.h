#pragma once

#include "BallsCoreIncludes.h"
#include "Containers/StringConv.h"
#include "CoreMinimal.h"

#include <string>
#include <string_view>

namespace BallsText {

inline FString FromUtf8(std::string_view Text) {
    const FUTF8ToTCHAR Converted(Text.data(), static_cast<int32>(Text.size()));
    return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
}

inline FString Grouped(BallCount Value) {
    FString Digits = FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Value));
    for (int32 InsertAt = Digits.Len() - 3; InsertAt > 0; InsertAt -= 3) {
        Digits.InsertAt(InsertAt, TEXT(','));
    }
    return Digits;
}

inline FString CompactCount(int32 Count) {
    if (Count >= 1'000'000) {
        return FString::Printf(TEXT("%dM"), Count / 1'000'000);
    }
    if (Count >= 10'000) {
        return FString::Printf(TEXT("%dk"), Count / 1'000);
    }
    return FString::FromInt(Count);
}

}
