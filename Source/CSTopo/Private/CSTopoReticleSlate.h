#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCSTopoReticle : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCSTopoReticle) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
