#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;
class SWrapBox;
class UCSTopoSurveySubsystem;
struct FCSTopoControlCodeDefinition;

class SCSTopoSurveyHud : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCSTopoSurveyHud) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UCSTopoSurveySubsystem>, SurveySubsystem)
        SLATE_EVENT(FSimpleDelegate, OnCollectShot)
        SLATE_EVENT(FSimpleDelegate, OnToggleFocus)
        SLATE_EVENT(FSimpleDelegate, OnUndo)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual bool SupportsKeyboardFocus() const override
    {
        return bCollectorInteractive;
    }

    virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
    virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

    void SetCollectorInteractive(bool bInteractive);
    void FocusCodeEditor();

private:
    TWeakObjectPtr<UCSTopoSurveySubsystem> SurveySubsystem;
    FSimpleDelegate OnCollectShot;
    FSimpleDelegate OnToggleFocus;
    FSimpleDelegate OnUndo;

    TSharedPtr<SEditableTextBox> CodeTextBox;
    TSharedPtr<SEditableTextBox> CodeSearchBox;
    TSharedPtr<SWrapBox> QuickCodeBox;
    TSharedPtr<SWrapBox> ControlCodeBox;
    TSharedPtr<SVerticalBox> RecentShotBox;

    FString ActionStatusMessage;
    FString CodeFilterText;
    FString PendingParameterControlCode;
    FString LastSyncedCode;
    FString LastRenderedActiveCode;
    FString LastRenderedFilterText;
    int32 LastRenderedPaletteCount = INDEX_NONE;
    int32 LastRenderedControlCodeCount = INDEX_NONE;
    int32 LastRenderedShotCount = INDEX_NONE;
    bool bUpdatingCodeText = false;
    bool bCollectorInteractive = false;

    void RefreshDynamicContent();
    void RefreshQuickCodes();
    void RefreshControlCodes();
    void RefreshRecentShots();
    void SyncCodeText();

    TSharedRef<SWidget> BuildSectionHeader(const FString& Label) const;
    TSharedRef<SWidget> BuildQuickCodeButton(const FString& Code, const FLinearColor& Color, bool bIsActive);
    TSharedRef<SWidget> BuildControlCodeButton(const FCSTopoControlCodeDefinition& Definition);
    TSharedRef<SWidget> BuildCodeCategoryLabel(const FString& Category) const;

    FReply HandleCollectShot();
    FReply HandleUndo();
    FReply HandleSplitFigure();
    FReply HandleCloseFigure();
    void HandleCodeFilterChanged(const FText& NewText);
    void HandleCodeFilterCommitted(const FText& NewText, ETextCommit::Type CommitType);
    void HandleCodeTextChanged(const FText& NewText);
    void HandleCodeTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

    FText GetProjectNameText() const;
    FText GetActiveSourceText() const;
    FText GetActiveCodeText() const;
    FText GetActiveCodeDetailText() const;
    FText GetActivePointTypeText() const;
    FText GetCollectorPointIdText() const;
    FText GetNavigationModeText() const;
    FText GetNextPointText() const;
    FText GetSurveySummaryText() const;
    FText GetFigureStatusText() const;
    FText GetHoverText() const;
    FText GetActionStatusText() const;
    FSlateColor GetHoverColor() const;
    FSlateColor GetActiveCodeSwatchColor() const;
};
