#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTopoTypes.h"
#include "CSTopoPointCloudToolbarWidget.generated.h"

class UButton;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class CSTOPO_API UCSTopoPointCloudToolbarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    void RefreshList();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    void SetStatusMessage(const FString& Message);

protected:
    UPROPERTY()
    TObjectPtr<UEditableTextBox> SourcePathBox;

    UPROPERTY()
    TObjectPtr<UScrollBox> CloudListBox;

    UPROPERTY()
    TObjectPtr<UTextBlock> StatusText;

private:
    UFUNCTION()
    void OnAddClicked();

    UFUNCTION()
    void OnRefreshClicked();

    UButton* MakeButton(const FString& Label);
};

UCLASS()
class CSTOPO_API UCSTopoPointCloudRowWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void InitializeRow(const FCSTopoPointCloudSource& InSource, UCSTopoPointCloudToolbarWidget* InToolbar);

private:
    UPROPERTY()
    FCSTopoPointCloudSource Source;

    UPROPERTY()
    TObjectPtr<UCSTopoPointCloudToolbarWidget> Toolbar;

    UFUNCTION()
    void OnSetActiveClicked();

    UFUNCTION()
    void OnToggleVisibleClicked();

    UFUNCTION()
    void OnToggleLoadedClicked();

    UFUNCTION()
    void OnConvertClicked();

    UFUNCTION()
    void OnRemoveClicked();

    UButton* MakeButton(const FString& Label);
};
