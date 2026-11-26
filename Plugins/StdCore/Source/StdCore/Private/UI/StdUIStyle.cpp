// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/StdUIStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Settings/StdProjectSettings.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UI/StdUITheme.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StdUIStyle)

FStdUIStyle::FStdUIStyle()
{
	// A usable default for a project that switches the style on without setting it: dark, flat, blue accent.
	AccentColor = FLinearColor(FColor(45, 127, 249));
	PanelColor = FLinearColor(FColor(8, 9, 11, 245));
	BackdropColor = FLinearColor(FColor(0, 0, 0, 225));
	FrameColor = FLinearColor(FColor(110, 116, 128));
	TextColor = FLinearColor(FColor(232, 234, 238));
	MutedTextColor = FLinearColor(FColor(140, 146, 158));
}

namespace StdUIStyleDetail
{
	static TOptional<FLinearColor> GAccent;
	static uint32 GVersion = 1;

	/** Textures made in code, rooted for the life of the process (a few kilobytes each, made once). */
	static TMap<uint32, UTexture2D*> GFrameTextures;
	static UTexture2D* GNoise = nullptr;
	static UTexture2D* GPatch = nullptr;

	static UTexture2D* MakeTexture(int32 Width, int32 Height, const TArray<FColor>& Pixels, TextureFilter Filter, const TCHAR* Name)
	{
		// A name of its own: a second texture under the same name in the transient package would replace the first.
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(Name)));
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = Filter;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->NeverStream = true;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Texture->AddToRoot();
		return Texture;
	}

	/** Corner size of a frame texture: room for the cut, the line and the marker. */
	static int32 CornerTexels(float Thickness, float Cut)
	{
		return FMath::CeilToInt(FMath::Max(Cut + Thickness + 2.f, Thickness + 12.f));
	}

	static UTexture2D* MakeFrameTexture(const FLinearColor& Fill, const FLinearColor& Frame, float T, float Cut, bool bMarker, const FLinearColor& Marker)
	{
		const int32 K = CornerTexels(T, Cut);
		const int32 Size = 2 * K + 2;
		const float S = static_cast<float>(Size);
		const float MarkerX0 = T + 5.f;
		const float MarkerX1 = MarkerX0 + 3.f;
		constexpr int32 Sub = 4;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
				for (int32 J = 0; J < Sub; ++J)
				{
					for (int32 I = 0; I < Sub; ++I)
					{
						const float PX = X + (I + 0.5f) / Sub;
						const float PY = Y + (J + 0.5f) / Sub;
						// Inside the shape: the square less the triangle cut at its top right corner.
						const float Diagonal = ((S - Cut) - (PX - PY)) * UE_INV_SQRT_2;
						if (Diagonal < 0.f)
						{
							continue;
						}
						const float Edge = FMath::Min(FMath::Min(PX, PY), FMath::Min(S - PX, S - PY));
						FLinearColor C = Fill;
						if (FMath::Min(Edge, Cut > 0.f ? Diagonal : Edge) < T)
						{
							C = Frame;
						}
						else if (bMarker && PX >= MarkerX0 && PX <= MarkerX1 && PY >= K && PY <= K + 2)
						{
							C = Marker;
						}
						Sum.R += C.R * C.A;
						Sum.G += C.G * C.A;
						Sum.B += C.B * C.A;
						Sum.A += C.A;
					}
				}
				const float Alpha = Sum.A / (Sub * Sub);
				const FLinearColor Out = Sum.A > 0.f ? FLinearColor(Sum.R / Sum.A, Sum.G / Sum.A, Sum.B / Sum.A, Alpha) : FLinearColor::Transparent;
				FColor Pixel = Out.ToFColor(true);
				Pixel.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Alpha * 255.f), 0, 255));
				Pixels[Y * Size + X] = Pixel;
			}
		}
		return MakeTexture(Size, Size, Pixels, TF_Bilinear, TEXT("StdUIFrame"));
	}
}

bool StdUIStyle::IsEnabled()
{
	return UStdProjectSettings::Get()->bUseStyle;
}

const FStdUIStyle& StdUIStyle::Get()
{
	return UStdProjectSettings::Get()->Style;
}

void StdUIStyle::SetAccentColor(const FLinearColor& Color)
{
	if (!StdUIStyleDetail::GAccent.IsSet() || !StdUIStyleDetail::GAccent.GetValue().Equals(Color))
	{
		StdUIStyleDetail::GAccent = Color;
		++StdUIStyleDetail::GVersion;
	}
}

void StdUIStyle::ClearAccentColor()
{
	if (StdUIStyleDetail::GAccent.IsSet())
	{
		StdUIStyleDetail::GAccent.Reset();
		++StdUIStyleDetail::GVersion;
	}
}

FLinearColor StdUIStyle::GetAccentColor()
{
	return StdUIStyleDetail::GAccent.IsSet() ? StdUIStyleDetail::GAccent.GetValue() : Get().AccentColor;
}

uint32 StdUIStyle::GetVersion()
{
	return StdUIStyleDetail::GVersion;
}

const UObject* StdUIStyle::GetFontObject()
{
	const FSoftObjectPath& Path = Get().Font;
	if (Path.IsNull())
	{
		return nullptr;
	}
	if (UObject* Loaded = Path.ResolveObject())
	{
		return Loaded;
	}
	return StdSettings::LoadSettingAsset(Path, TEXT("Style.Font"), TEXT("(empty: the engine's default font)"));
}

FSlateFontInfo StdUIStyle::MakeFont(int32 Size, int32 LetterSpacing)
{
	const int32 ClampedSize = FMath::Clamp(Size, 1, 200);
	FSlateFontInfo Font;
	if (IsEnabled())
	{
		if (const UObject* FontObject = GetFontObject())
		{
			Font = FSlateFontInfo(FontObject, ClampedSize, Get().Typeface);
		}
		else
		{
			Font = FCoreStyle::GetDefaultFontStyle("Regular", ClampedSize);
		}
	}
	else if (const UStdUITheme* Theme = StdUITheme::Get())
	{
		Font = Theme->BodyFont;
		Font.Size = ClampedSize;
	}
	else
	{
		Font = FCoreStyle::GetDefaultFontStyle("Regular", ClampedSize);
	}
	Font.LetterSpacing = LetterSpacing;
	return Font;
}

FSlateBrush StdUIStyle::MakeFrameBrush(const FLinearColor& Fill, const FLinearColor& Frame, float Thickness, float Cut, bool bMarker, const FLinearColor& MarkerColor)
{
	using namespace StdUIStyleDetail;
	const float T = FMath::Clamp(Thickness, 0.5f, 8.f);
	const float C = FMath::Clamp(Cut, 0.f, 64.f);
	uint32 Key = GetTypeHash(Fill);
	Key = HashCombine(Key, GetTypeHash(Frame));
	Key = HashCombine(Key, GetTypeHash(T));
	Key = HashCombine(Key, GetTypeHash(C));
	Key = HashCombine(Key, GetTypeHash(bMarker));
	Key = HashCombine(Key, GetTypeHash(bMarker ? MarkerColor : FLinearColor::Transparent));
	UTexture2D*& Texture = GFrameTextures.FindOrAdd(Key);
	if (!Texture)
	{
		Texture = MakeFrameTexture(Fill, Frame, T, C, bMarker, MarkerColor);
	}
	FSlateBrush Brush;
	if (!Texture)
	{
		// No texture (no rendering): a plain box of the fill.
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(Fill);
		return Brush;
	}
	const int32 K = CornerTexels(T, C);
	const float Size = static_cast<float>(2 * K + 2);
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2D(Size, Size);
	Brush.DrawAs = ESlateBrushDrawType::Box;
	Brush.Margin = FMargin(K / Size);
	Brush.TintColor = FSlateColor(FLinearColor::White);
	return Brush;
}

FSlateBrush StdUIStyle::MakeBoxBrush(const FLinearColor& Fill, float Radius)
{
	if (IsEnabled())
	{
		const FStdUIStyle& Style = Get();
		return MakeFrameBrush(Fill, Style.FrameColor, Style.FrameThickness, Style.CornerCut);
	}
	return FSlateRoundedBoxBrush(Fill, Radius);
}

FString StdUIStyle::DescribeFont(const FSlateFontInfo& Font)
{
	const UObject* Object = Font.FontObject.Get();
	return FString::Printf(TEXT("%s/%s %.0f"), Object ? *Object->GetName() : TEXT("(engine default)"), *Font.TypefaceFontName.ToString(), static_cast<float>(Font.Size));
}

UTexture2D* StdUIStyle::GetNoiseTexture()
{
	using namespace StdUIStyleDetail;
	if (!GNoise)
	{
		constexpr int32 Width = 2048;
		constexpr int32 Height = 1024;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		uint32 State = 0x9E3779B9u;
		for (FColor& Pixel : Pixels)
		{
			// xorshift32: fast, uniform enough for grain.
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			const uint8 Level = static_cast<uint8>(State >> 24);
			Pixel = FColor(Level, Level, Level, 255);
		}
		GNoise = MakeTexture(Width, Height, Pixels, TF_Nearest, TEXT("StdUINoise"));
	}
	return GNoise;
}

UTexture2D* StdUIStyle::GetSoftPatchTexture()
{
	using namespace StdUIStyleDetail;
	if (!GPatch)
	{
		constexpr int32 Size = 64;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);
		const auto Ramp = [](float D)
		{
			// 0 at the edge, 1 at the middle (half the texture), smooth at both ends.
			const float U = FMath::Clamp(D / (Size * 0.5f), 0.f, 1.f);
			return U * U * (3.f - 2.f * U);
		};
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float DX = FMath::Min(X + 0.5f, Size - X - 0.5f);
				const float DY = FMath::Min(Y + 0.5f, Size - Y - 0.5f);
				const float Alpha = Ramp(DX) * Ramp(DY);
				Pixels[Y * Size + X] = FColor(255, 255, 255, static_cast<uint8>(FMath::RoundToInt(Alpha * 255.f)));
			}
		}
		GPatch = MakeTexture(Size, Size, Pixels, TF_Bilinear, TEXT("StdUISoftPatch"));
	}
	return GPatch;
}
