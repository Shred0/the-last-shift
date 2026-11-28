// SPDX-License-Identifier: MIT

// tls.AudioLoopCheck [Wav...] (the editor; no map needed): the sounds that last, each one file played once, looping. For
// each sound that lasts (the Motion slot's, the Interruption slot's, the ChamberHum slot's) it reads the samples imported
// into its sound wave and measures the two things the ear finds in a sound played again and again:
//
// - Steady: the level (RMS) in windows of WindowMs (100 ms) stepped by StepMs (50 ms) along the whole file and across its
//   seam (the file read as a ring) stays within SteadyDb (2 dB) of the file's mean level. The motion's and the
//   interruption's must; the hum's level moves by design (its slow beat and swell): its numbers are given, not judged.
// - Seamless: the step from the last sample to the first is no larger than the largest step between two neighbouring
//   samples anywhere else in the file.
//
// And that the file is as long as it must be (the motion's at least 8 s, so that the longest motion, 4 s, never reaches the
// seam at the hatches' pitch; the interruption's at least 4 s, longer than StaticSeconds + StaticFadeSeconds), mono, 48 kHz,
// and that its sound wave loops. One line per file with its numbers. The door's short sound (the Door slot, which the timed
// motions looped before) and the WAV files given (default: the interruption's placeholder before it was made to loop, from
// the sources' Audio folder when it is on disk) are measured too, as notes, not judged. The same measure runs in
// make_loops.py on the WAV files it writes. Editor builds only (the imported samples are editor data).

#include "Audio.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Run/TheLastShiftSettings.h"
#include "Sound/SoundWave.h"
#include "TheLastShift.h"

#if !UE_BUILD_SHIPPING && WITH_EDITOR

namespace AudioLoopCheck
{
	static constexpr float WindowMs = 100.f;
	static constexpr float StepMs = 50.f;
	static constexpr float SteadyDb = 2.f;
	static constexpr int32 WantRate = 48000;

	struct FMeasure
	{
		int32 Frames = 0;
		int32 Rate = 0;
		int32 Channels = 0;
		float Seconds = 0.f;
		float MeanDbfs = 0.f;
		int32 Windows = 0;
		float MinDb = 0.f;
		float MaxDb = 0.f;
		/** The first and the last 10 ms against the mean (a note: an attack or a decay shorter than a window). */
		float FirstEdgeDb = 0.f;
		float LastEdgeDb = 0.f;
		int32 SeamStep = 0;
		int32 InnerStep = 0;
		bool bSteady = false;
		bool bSeamless = false;
	};

	static int32 Passed = 0;
	static int32 Failed = 0;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? Passed : Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.AudioLoopCheck   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("tls.AudioLoopCheck   ..  %s"), *What);
	}

	static float Db(double MeanSquare, double Reference)
	{
		return static_cast<float>(10.0 * FMath::LogX(10.0, FMath::Max(MeanSquare, 1.e-12) / FMath::Max(Reference, 1.e-12)));
	}

	/** The measure of the first channel of interleaved 16-bit samples. */
	static FMeasure Measure(const int16* Interleaved, int32 Frames, int32 Channels, int32 Rate)
	{
		FMeasure M;
		M.Frames = Frames;
		M.Rate = Rate;
		M.Channels = Channels;
		M.Seconds = Rate > 0 ? static_cast<float>(Frames) / Rate : 0.f;
		if (Frames < 2 || Channels < 1 || Rate <= 0)
		{
			return M;
		}
		TArray<double> Square;
		Square.SetNumUninitialized(Frames);
		double Sum = 0.0;
		for (int32 I = 0; I < Frames; ++I)
		{
			const double S = Interleaved[I * Channels];
			Square[I] = S * S;
			Sum += Square[I];
		}
		const double Mean = Sum / Frames;
		M.MeanDbfs = Db(Mean, 32767.0 * 32767.0);
		const int32 Window = FMath::Max(1, FMath::RoundToInt(Rate * WindowMs / 1000.f));
		const int32 Step = FMath::Max(1, FMath::RoundToInt(Rate * StepMs / 1000.f));
		// Prefix sums over the ring: the file and then its first window again, for the windows across the seam.
		TArray<double> Prefix;
		Prefix.SetNumUninitialized(Frames + Window + 1);
		Prefix[0] = 0.0;
		for (int32 I = 0; I < Frames + Window; ++I)
		{
			Prefix[I + 1] = Prefix[I] + Square[I % Frames];
		}
		M.MinDb = TNumericLimits<float>::Max();
		M.MaxDb = TNumericLimits<float>::Lowest();
		for (int32 Start = 0; Start < Frames; Start += Step)
		{
			const float L = Db((Prefix[Start + Window] - Prefix[Start]) / Window, Mean);
			M.MinDb = FMath::Min(M.MinDb, L);
			M.MaxDb = FMath::Max(M.MaxDb, L);
			++M.Windows;
		}
		const int32 Edge = FMath::Min(FMath::Max(1, Rate / 100), Frames);
		M.FirstEdgeDb = Db(Prefix[Edge] / Edge, Mean);
		M.LastEdgeDb = Db((Prefix[Frames] - Prefix[Frames - Edge]) / Edge, Mean);
		for (int32 I = 0; I + 1 < Frames; ++I)
		{
			M.InnerStep = FMath::Max(M.InnerStep, FMath::Abs(Interleaved[(I + 1) * Channels] - Interleaved[I * Channels]));
		}
		M.SeamStep = FMath::Abs(Interleaved[0] - Interleaved[(Frames - 1) * Channels]);
		M.bSteady = FMath::Max(FMath::Abs(M.MinDb), FMath::Abs(M.MaxDb)) <= SteadyDb;
		M.bSeamless = M.SeamStep <= M.InnerStep;
		return M;
	}

	static FString Describe(const FMeasure& M)
	{
		return FString::Printf(TEXT("%.3f s, %d Hz, %d channel(s), level %.1f dBFS; %d windows of %.0f ms every %.0f ms: %+.2f to %+.2f dB of the mean (%s); seam step %d, largest step inside %d (%s); first/last 10 ms %+.1f/%+.1f dB"),
			M.Seconds, M.Rate, M.Channels, M.MeanDbfs, M.Windows, WindowMs, StepMs, M.MinDb, M.MaxDb, M.bSteady ? TEXT("steady") : TEXT("NOT steady"), M.SeamStep, M.InnerStep,
			M.bSeamless ? TEXT("seamless") : TEXT("NOT seamless"), M.FirstEdgeDb, M.LastEdgeDb);
	}

	/** The samples imported into Wave (editor data). */
	static bool MeasureWave(const USoundWave* Wave, FMeasure& Out)
	{
		TArray<uint8> Pcm;
		uint32 Rate = 0;
		uint16 Channels = 0;
		if (!Wave || !Wave->GetImportedSoundWaveData(Pcm, Rate, Channels) || Channels == 0)
		{
			return false;
		}
		const int32 Frames = Pcm.Num() / (sizeof(int16) * Channels);
		Out = Measure(reinterpret_cast<const int16*>(Pcm.GetData()), Frames, Channels, static_cast<int32>(Rate));
		return Frames > 1;
	}

	/** A 16-bit WAV file on disk. */
	static bool MeasureFile(const FString& Path, FMeasure& Out)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
		{
			return false;
		}
		FWaveModInfo Info;
		if (!Info.ReadWaveInfo(Bytes.GetData(), Bytes.Num()) || *Info.pBitsPerSample != 16 || *Info.pChannels == 0)
		{
			return false;
		}
		const int32 Channels = *Info.pChannels;
		const int32 Frames = Info.SampleDataSize / (sizeof(int16) * Channels);
		Out = Measure(reinterpret_cast<const int16*>(Info.SampleDataStart), Frames, Channels, static_cast<int32>(*Info.pSamplesPerSec));
		return Frames > 1;
	}

	/** One sound that lasts, by slot: its file's numbers, and what it must be. MinSeconds 0: no length asked. */
	static void CheckSlot(EGameSound Slot, float MinSeconds, bool bJudgeSteady)
	{
		const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
		const TSoftObjectPtr<USoundBase>& Ref = Audio.GetSound(Slot);
		const USoundWave* Wave = Cast<USoundWave>(Ref.LoadSynchronous());
		FMeasure M;
		const bool bRead = MeasureWave(Wave, M);
		Check(bRead, FString::Printf(TEXT("%s (%s): its sound wave's samples read: %s"), LexToString(Slot), Ref.IsNull() ? TEXT("empty slot") : *Ref.ToString(),
			bRead ? *Describe(M) : TEXT("no samples")));
		if (!bRead)
		{
			return;
		}
		Check(M.Channels == 1 && M.Rate == WantRate && Wave->bLooping && M.Seconds >= MinSeconds - 0.001f,
			FString::Printf(TEXT("%s: mono (%d), %d Hz (%d), its sound wave loops (%s), %.3f s (at least %.2f s)"), LexToString(Slot), M.Channels, WantRate, M.Rate,
				Wave->bLooping ? TEXT("yes") : TEXT("no"), M.Seconds, MinSeconds));
		if (bJudgeSteady)
		{
			Check(M.bSteady, FString::Printf(TEXT("%s: steady, every window within %.1f dB of the mean (%+.2f to %+.2f dB)"), LexToString(Slot), SteadyDb, M.MinDb, M.MaxDb));
		}
		else
		{
			Note(FString::Printf(TEXT("%s: its level moves by design, not judged on steady (%+.2f to %+.2f dB of the mean)"), LexToString(Slot), M.MinDb, M.MaxDb));
		}
		Check(M.bSeamless, FString::Printf(TEXT("%s: seamless, the step from the last sample to the first %d, the largest step inside %d"), LexToString(Slot), M.SeamStep, M.InnerStep));
	}

	static void Run(const TArray<FString>& Args)
	{
		Passed = 0;
		Failed = 0;
		const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
		const float Static = Settings->GetStaticSeconds() + Settings->GetStaticFadeSeconds();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.AudioLoopCheck: windows %.0f ms every %.0f ms across the seam, steady within %.1f dB; StaticSeconds + StaticFadeSeconds %.2f s."),
			WindowMs, StepMs, SteadyDb, Static);
		CheckSlot(EGameSound::Motion, 8.f, true);
		CheckSlot(EGameSound::Interruption, FMath::Max(4.f, Static), true);
		CheckSlot(EGameSound::ChamberHum, 12.f, false);

		// Notes: the door's short sound (looped by the timed motions before), and the files given or the old interruption's.
		FMeasure M;
		const USoundWave* Door = Cast<USoundWave>(Settings->Audio.GetSound(EGameSound::Door).LoadSynchronous());
		if (MeasureWave(Door, M))
		{
			Note(FString::Printf(TEXT("Door (the short sound, %s): %s"), *GetNameSafe(Door), *Describe(M)));
		}
		TArray<FString> Files = Args;
		if (Files.Num() == 0)
		{
			const FString Old = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../TheLastShift-Sources/Audio/Placeholders/SW_Interruption.wav"));
			if (FPaths::FileExists(Old))
			{
				Files.Add(Old);
			}
		}
		for (const FString& File : Files)
		{
			if (MeasureFile(File, M))
			{
				Note(FString::Printf(TEXT("file %s: %s%s"), *FPaths::GetCleanFilename(File), *Describe(M),
					M.bSteady && M.bSeamless ? TEXT(" -- passes both measures") : TEXT(" -- fails the measure")));
			}
			else
			{
				Note(FString::Printf(TEXT("file %s: not read (missing, or not a 16-bit WAV)"), *File));
			}
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.AudioLoopCheck: %d passed, %d failed."), Passed, Failed);
	}

	static FAutoConsoleCommandWithWorldAndArgs GAudioLoopCheckCommand(
		TEXT("tls.AudioLoopCheck"),
		TEXT("tls.AudioLoopCheck [Wav...]: the sounds that last (Motion, Interruption, ChamberHum): steady (100 ms windows within 2 dB of the mean, across the seam), seamless, length, mono, 48 kHz, looping. Editor; no map needed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*) { Run(Args); }));
}

#endif // !UE_BUILD_SHIPPING && WITH_EDITOR
