#include "CSTopoPointCloudToolbarWidget.h"

#include "CSTopoSurveySubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/Paths.h"

void UCSTopoPointCloudToolbarWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PointCloudToolbarRoot"));
    WidgetTree->RootWidget = Root;

    UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
    Title->SetText(FText::FromString(TEXT("Point Clouds")));
    Root->AddChildToVerticalBox(Title);

    UHorizontalBox* AddRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AddCloudRow"));
    Root->AddChildToVerticalBox(AddRow);

    SourcePathBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SourcePathBox"));
    SourcePathBox->SetHintText(FText::FromString(TEXT("LAS/LAZ/COPC path")));
    UHorizontalBoxSlot* PathSlot = AddRow->AddChildToHorizontalBox(SourcePathBox);
    PathSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
    PathSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    UButton* AddButton = MakeButton(TEXT("Add"));
    AddButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudToolbarWidget::OnAddClicked);
    AddRow->AddChildToHorizontalBox(AddButton);

    UButton* RefreshButton = MakeButton(TEXT("Refresh"));
    RefreshButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudToolbarWidget::OnRefreshClicked);
    AddRow->AddChildToHorizontalBox(RefreshButton);

    CloudListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CloudList"));
    UVerticalBoxSlot* ListSlot = Root->AddChildToVerticalBox(CloudListBox);
    ListSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));

    StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
    StatusText->SetAutoWrapText(true);
    Root->AddChildToVerticalBox(StatusText);

    RefreshList();
}

void UCSTopoPointCloudToolbarWidget::RefreshList()
{
    if (CloudListBox == nullptr)
    {
        return;
    }

    CloudListBox->ClearChildren();

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        SetStatusMessage(TEXT("Survey subsystem is not available."));
        return;
    }

    const TArray<FCSTopoPointCloudSource> Clouds = Survey->GetPointClouds();
    if (Clouds.IsEmpty())
    {
        SetStatusMessage(TEXT("No point clouds loaded."));
        return;
    }

    for (const FCSTopoPointCloudSource& Cloud : Clouds)
    {
        UCSTopoPointCloudRowWidget* Row = WidgetTree->ConstructWidget<UCSTopoPointCloudRowWidget>(UCSTopoPointCloudRowWidget::StaticClass());
        Row->InitializeRow(Cloud, this);
        CloudListBox->AddChild(Row);
    }

    SetStatusMessage(FString::Printf(TEXT("%d point cloud(s) in project."), Clouds.Num()));
}

void UCSTopoPointCloudToolbarWidget::SetStatusMessage(const FString& Message)
{
    if (StatusText != nullptr)
    {
        StatusText->SetText(FText::FromString(Message));
    }
}

void UCSTopoPointCloudToolbarWidget::OnAddClicked()
{
    const FString SourcePath = SourcePathBox ? SourcePathBox->GetText().ToString() : FString();
    if (SourcePath.IsEmpty())
    {
        SetStatusMessage(TEXT("Enter a LAS/LAZ/COPC path first."));
        return;
    }

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        SetStatusMessage(TEXT("Survey subsystem is not available."));
        return;
    }

    FCSTopoPointCloudSource AddedSource;
    FString ErrorMessage;
    const FString CacheDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("cache"));
    if (!Survey->AddPointCloud(SourcePath, CacheDir, AddedSource, ErrorMessage))
    {
        SetStatusMessage(ErrorMessage);
        return;
    }

    SetStatusMessage(FString::Printf(TEXT("Added %s"), *FPaths::GetCleanFilename(SourcePath)));
    RefreshList();
}

void UCSTopoPointCloudToolbarWidget::OnRefreshClicked()
{
    RefreshList();
}

UButton* UCSTopoPointCloudToolbarWidget::MakeButton(const FString& Label)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetText(FText::FromString(Label));
    Text->SetJustification(ETextJustify::Center);
    Button->AddChild(Text);
    return Button;
}

void UCSTopoPointCloudRowWidget::InitializeRow(const FCSTopoPointCloudSource& InSource, UCSTopoPointCloudToolbarWidget* InToolbar)
{
    Source = InSource;
    Toolbar = InToolbar;

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    WidgetTree->RootWidget = Root;

    const FString Name = FPaths::GetCleanFilename(Source.SourcePath);
    const FString Summary = FString::Printf(
        TEXT("%s%s | %lld pts | %s | %s"),
        Source.bIsActive ? TEXT("* ") : TEXT(""),
        *Name,
        Source.PointCount,
        Source.bLoaded ? TEXT("Loaded") : TEXT("Unloaded"),
        Source.bVisible ? TEXT("Visible") : TEXT("Hidden"));

    UTextBlock* SummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    SummaryText->SetText(FText::FromString(Summary));
    SummaryText->SetAutoWrapText(true);
    Root->AddChildToVerticalBox(SummaryText);

    UTextBlock* CacheText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    CacheText->SetText(FText::FromString(FString::Printf(TEXT("Cache: %s"), Source.CachePath.IsEmpty() ? TEXT("(none)") : *Source.CachePath)));
    CacheText->SetAutoWrapText(true);
    Root->AddChildToVerticalBox(CacheText);

    UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Root->AddChildToVerticalBox(Buttons);

    UButton* ActiveButton = MakeButton(TEXT("Active"));
    ActiveButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudRowWidget::OnSetActiveClicked);
    Buttons->AddChildToHorizontalBox(ActiveButton);

    UButton* VisibleButton = MakeButton(Source.bVisible ? TEXT("Hide") : TEXT("Show"));
    VisibleButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudRowWidget::OnToggleVisibleClicked);
    Buttons->AddChildToHorizontalBox(VisibleButton);

    UButton* LoadedButton = MakeButton(Source.bLoaded ? TEXT("Unload") : TEXT("Load"));
    LoadedButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudRowWidget::OnToggleLoadedClicked);
    Buttons->AddChildToHorizontalBox(LoadedButton);

    UButton* ConvertButton = MakeButton(TEXT("COPC"));
    ConvertButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudRowWidget::OnConvertClicked);
    Buttons->AddChildToHorizontalBox(ConvertButton);

    UButton* RemoveButton = MakeButton(TEXT("Remove"));
    RemoveButton->OnClicked.AddDynamic(this, &UCSTopoPointCloudRowWidget::OnRemoveClicked);
    Buttons->AddChildToHorizontalBox(RemoveButton);
}

void UCSTopoPointCloudRowWidget::OnSetActiveClicked()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        Survey->SetActivePointCloud(Source.SourceId);
    }
    if (Toolbar != nullptr)
    {
        Toolbar->RefreshList();
    }
}

void UCSTopoPointCloudRowWidget::OnToggleVisibleClicked()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        Survey->SetPointCloudVisible(Source.SourceId, !Source.bVisible);
    }
    if (Toolbar != nullptr)
    {
        Toolbar->RefreshList();
    }
}

void UCSTopoPointCloudRowWidget::OnToggleLoadedClicked()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        Survey->SetPointCloudLoaded(Source.SourceId, !Source.bLoaded);
    }
    if (Toolbar != nullptr)
    {
        Toolbar->RefreshList();
    }
}

void UCSTopoPointCloudRowWidget::OnConvertClicked()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    FString Message;
    if (Survey == nullptr)
    {
        Message = TEXT("Survey subsystem is not available.");
    }
    else
    {
        Survey->StartCopcCacheBuild(Source.SourceId, Message);
    }
    if (Toolbar != nullptr)
    {
        Toolbar->SetStatusMessage(Message);
        Toolbar->RefreshList();
    }
}

void UCSTopoPointCloudRowWidget::OnRemoveClicked()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        Survey->RemovePointCloud(Source.SourceId);
    }
    if (Toolbar != nullptr)
    {
        Toolbar->RefreshList();
    }
}

UButton* UCSTopoPointCloudRowWidget::MakeButton(const FString& Label)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetText(FText::FromString(Label));
    Text->SetJustification(ETextJustify::Center);
    Button->AddChild(Text);
    return Button;
}
