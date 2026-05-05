#include "CSTopoExporter.h"

#include "Misc/FileHelper.h"

namespace
{
FString CsvEscape(const FString& Value)
{
    FString Escaped = Value;
    Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
    if (Escaped.Contains(TEXT(",")) || Escaped.Contains(TEXT("\"")) || Escaped.Contains(TEXT("\n")))
    {
        Escaped = FString::Printf(TEXT("\"%s\""), *Escaped);
    }
    return Escaped;
}

const FCSTopoShotRecord* FindShotByNumber(const FCSTopoProjectDocument& Project, int32 PointNumber)
{
    return Project.Shots.FindByPredicate([PointNumber](const FCSTopoShotRecord& Shot)
    {
        return Shot.PointNumber == PointNumber;
    });
}
}

bool UCSTopoExporter::ExportCsv(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage)
{
    TArray<FString> Lines;
    Lines.Add(TEXT("PointNumber,Northing,Easting,Elevation,Code,FigureId,Notes"));

    for (const FCSTopoShotRecord& Shot : Project.Shots)
    {
        Lines.Add(FString::Printf(
            TEXT("%d,%.4f,%.4f,%.4f,%s,%s,%s"),
            Shot.PointNumber,
            Shot.Northing,
            Shot.Easting,
            Shot.Elevation,
            *CsvEscape(Shot.Code),
            *Shot.FigureId.ToString(EGuidFormats::DigitsWithHyphens),
            *CsvEscape(FString::Printf(TEXT("fit=%s residual=%.4f"), *Shot.FitType, Shot.FitResidual))));
    }

    if (!FFileHelper::SaveStringArrayToFile(Lines, *FilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to write CSV: %s"), *FilePath);
        return false;
    }

    ErrorMessage.Empty();
    return true;
}

bool UCSTopoExporter::ExportDxf(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage)
{
    FString Dxf;
    Dxf += TEXT("0\nSECTION\n2\nHEADER\n0\nENDSEC\n");
    Dxf += TEXT("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n");

    for (const FCSTopoCodeStyle& Style : Project.CodePalette)
    {
        Dxf += FString::Printf(TEXT("0\nLAYER\n2\n%s\n70\n0\n62\n7\n6\nCONTINUOUS\n"), *Style.LayerName);
    }

    Dxf += TEXT("0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n");

    for (const FCSTopoShotRecord& Shot : Project.Shots)
    {
        const FString Layer = Shot.Code.IsEmpty() ? TEXT("POINTS") : Shot.Code;
        Dxf += FString::Printf(
            TEXT("0\nPOINT\n8\n%s\n10\n%.4f\n20\n%.4f\n30\n%.4f\n"),
            *Layer,
            Shot.Easting,
            Shot.Northing,
            Shot.Elevation);
    }

    for (const FCSTopoFigureRecord& Figure : Project.Figures)
    {
        if (Figure.PointNumbers.Num() < 2)
        {
            continue;
        }

        const FString Layer = Figure.LayerName.IsEmpty() ? Figure.Code : Figure.LayerName;
        Dxf += FString::Printf(TEXT("0\nPOLYLINE\n8\n%s\n66\n1\n70\n8\n"), *Layer);

        for (int32 PointNumber : Figure.PointNumbers)
        {
            const FCSTopoShotRecord* Shot = FindShotByNumber(Project, PointNumber);
            if (Shot == nullptr)
            {
                continue;
            }

            Dxf += FString::Printf(
                TEXT("0\nVERTEX\n8\n%s\n10\n%.4f\n20\n%.4f\n30\n%.4f\n"),
                *Layer,
                Shot->Easting,
                Shot->Northing,
                Shot->Elevation);
        }

        if (Figure.bLoopClosed)
        {
            const FCSTopoShotRecord* Shot = FindShotByNumber(Project, Figure.PointNumbers[0]);
            if (Shot != nullptr)
            {
                Dxf += FString::Printf(
                    TEXT("0\nVERTEX\n8\n%s\n10\n%.4f\n20\n%.4f\n30\n%.4f\n"),
                    *Layer,
                    Shot->Easting,
                    Shot->Northing,
                    Shot->Elevation);
            }
        }

        Dxf += TEXT("0\nSEQEND\n");
    }

    Dxf += TEXT("0\nENDSEC\n0\nEOF\n");

    if (!FFileHelper::SaveStringToFile(Dxf, *FilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to write DXF: %s"), *FilePath);
        return false;
    }

    ErrorMessage.Empty();
    return true;
}
