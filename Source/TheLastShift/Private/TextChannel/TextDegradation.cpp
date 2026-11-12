// SPDX-License-Identifier: MIT

#include "TextChannel/TextDegradation.h"

#include "Misc/Crc.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextDegradation)

namespace TextDegradationImpl
{
	enum class EPiece : uint8
	{
		Word,
		Number,
		Token,
		Space,
		Other
	};

	struct FPiece
	{
		EPiece Kind = EPiece::Other;
		FString Text;
	};

	/** Words (letters, with inner apostrophes), numbers (digits, with inner . or ,), {tokens}, runs of spaces, single other characters. */
	static TArray<FPiece> Split(const FString& S)
	{
		TArray<FPiece> Out;
		const int32 N = S.Len();
		int32 I = 0;
		while (I < N)
		{
			const TCHAR C = S[I];
			int32 J = I + 1;
			EPiece Kind = EPiece::Other;
			if (C == TEXT('{'))
			{
				int32 K = I + 1;
				while (K < N && (FChar::IsAlnum(S[K]) || S[K] == TEXT('_')))
				{
					++K;
				}
				if (K < N && K > I + 1 && S[K] == TEXT('}'))
				{
					Kind = EPiece::Token;
					J = K + 1;
				}
			}
			else if (FChar::IsDigit(C))
			{
				Kind = EPiece::Number;
				while (J < N && (FChar::IsDigit(S[J]) || ((S[J] == TEXT('.') || S[J] == TEXT(',')) && J + 1 < N && FChar::IsDigit(S[J + 1]))))
				{
					++J;
				}
			}
			else if (FChar::IsAlpha(C))
			{
				Kind = EPiece::Word;
				while (J < N && (FChar::IsAlpha(S[J]) || (S[J] == TEXT('\'') && J + 1 < N && FChar::IsAlpha(S[J + 1]))))
				{
					++J;
				}
			}
			else if (FChar::IsWhitespace(C))
			{
				Kind = EPiece::Space;
				while (J < N && FChar::IsWhitespace(S[J]))
				{
					++J;
				}
			}
			Out.Add({ Kind, S.Mid(I, J - I) });
			I = J;
		}
		return Out;
	}

	static FString Join(const TArray<FPiece>& Pieces)
	{
		FString Out;
		for (const FPiece& Piece : Pieces)
		{
			Out += Piece.Text;
		}
		return Out;
	}

	static bool IsArticle(const FString& Word)
	{
		return Word.Equals(TEXT("a"), ESearchCase::IgnoreCase) || Word.Equals(TEXT("an"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("the"), ESearchCase::IgnoreCase);
	}

	/** Removes the whole words a, an, the with one space next to each; a capital moves to the word that follows. */
	static void RemoveArticles(TArray<FPiece>& Pieces)
	{
		for (int32 I = 0; I < Pieces.Num(); )
		{
			if (Pieces[I].Kind != EPiece::Word || !IsArticle(Pieces[I].Text))
			{
				++I;
				continue;
			}
			const bool bCapital = FChar::IsUpper(Pieces[I].Text[0]);
			Pieces.RemoveAt(I);
			if (Pieces.IsValidIndex(I) && Pieces[I].Kind == EPiece::Space)
			{
				Pieces.RemoveAt(I);
			}
			else if (Pieces.IsValidIndex(I - 1) && Pieces[I - 1].Kind == EPiece::Space)
			{
				Pieces.RemoveAt(I - 1);
				--I;
			}
			if (bCapital && Pieces.IsValidIndex(I) && Pieces[I].Kind == EPiece::Word && !Pieces[I].Text.IsEmpty())
			{
				Pieces[I].Text[0] = FChar::ToUpper(Pieces[I].Text[0]);
			}
		}
	}

	static bool IsVowel(TCHAR C)
	{
		const TCHAR L = FChar::ToLower(C);
		return L == TEXT('a') || L == TEXT('e') || L == TEXT('i') || L == TEXT('o') || L == TEXT('u');
	}

	/** Lowercase words; words longer than three letters lose their inner vowels. */
	static void Abbreviate(TArray<FPiece>& Pieces)
	{
		for (FPiece& Piece : Pieces)
		{
			if (Piece.Kind != EPiece::Word)
			{
				continue;
			}
			FString Word = Piece.Text.ToLower();
			if (Word.Len() > 3)
			{
				FString Kept;
				Kept.AppendChar(Word[0]);
				for (int32 I = 1; I < Word.Len() - 1; ++I)
				{
					if (!IsVowel(Word[I]))
					{
						Kept.AppendChar(Word[I]);
					}
				}
				Kept.AppendChar(Word[Word.Len() - 1]);
				Word = MoveTemp(Kept);
			}
			Piece.Text = MoveTemp(Word);
		}
	}

	/**
	 * Keeps the opening of about one word in three (chosen from the word and its position, so always the same), every
	 * number and every token, separated by single spaces; punctuation goes.
	 */
	static FString Scatter(const TArray<FPiece>& Pieces)
	{
		TArray<FString> Kept;
		int32 WordIndex = 0;
		for (const FPiece& Piece : Pieces)
		{
			if (Piece.Kind == EPiece::Number || Piece.Kind == EPiece::Token)
			{
				Kept.Add(Piece.Text);
			}
			else if (Piece.Kind == EPiece::Word)
			{
				const uint32 Hash = FCrc::StrCrc32(*Piece.Text) + static_cast<uint32>(WordIndex) * 7u;
				if (Hash % 3u == 0u)
				{
					const int32 Length = FMath::Clamp(Piece.Text.Len() / 2, 1, 3);
					Kept.Add(Piece.Text.Left(Length));
				}
				++WordIndex;
			}
		}
		return FString::Join(Kept, TEXT(" "));
	}
}

ETextDegradation UTextDegradationLibrary::LevelFromBattery(double Fraction, float NoArticlesBelow, float AbbreviatedBelow, float ScatteredBelow, float SilentBelow)
{
	// Compared in float, the type of the thresholds, so that a fraction equal to a threshold is never below it.
	const float F = static_cast<float>(Fraction);
	if (F < SilentBelow)
	{
		return ETextDegradation::Silent;
	}
	if (F < ScatteredBelow)
	{
		return ETextDegradation::Scattered;
	}
	if (F < AbbreviatedBelow)
	{
		return ETextDegradation::Abbreviated;
	}
	if (F < NoArticlesBelow)
	{
		return ETextDegradation::NoArticles;
	}
	return ETextDegradation::Full;
}

FString UTextDegradationLibrary::Degrade(const FString& Text, ETextDegradation Level)
{
	using namespace TextDegradationImpl;
	if (Level == ETextDegradation::Full)
	{
		return Text;
	}
	if (Level == ETextDegradation::Silent)
	{
		return FString();
	}
	TArray<FPiece> Pieces = Split(Text);
	RemoveArticles(Pieces);
	if (Level == ETextDegradation::NoArticles)
	{
		return Join(Pieces);
	}
	Abbreviate(Pieces);
	if (Level == ETextDegradation::Abbreviated)
	{
		return Join(Pieces);
	}
	return Scatter(Pieces);
}
