#pragma once

#include "reaper_plugin_functions.h"
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <mutex>
#include "JuceHeader.h"

/*
	A slightly silly class that imports and plays back Commodore64 SID tune files by first converting them into wav-files using the
	SID2WAV command line program. Unfortunately Windows-only because SID2WAV does not exist for macOs. It is open source but
	the code is in such ancient and apparently non standard-C++ style that it does not build with Clang.

	This thing is perhaps not hugely useful as such, but shows some principles how something similar could be implemented for other audio 
	media types where directly using a library for decoding is not desired for some reason. (The code dependencies needed to do what
	SID2WAV does would complicate building the Reaper plugin immensely. We might also really end up just rendering into 
	wav-files anyway because playing and especially seeking SIDs is a complicated business. By prerendering into wav-files we
	get performant and predictable results.)
*/

class SID_PCM_Source : public PCM_source, public MultiTimer
{
public:

	SID_PCM_Source();
	~SID_PCM_Source();

	void timerCallback(int id) override;

	// Inherited via PCM_source
	PCM_source * Duplicate() override;

	bool IsAvailable() override;

	const char * GetType() override;

	bool SetFileName(const char * newfn) override;

	const char *GetFileName() override;

	int GetNumChannels() override;

	double GetSampleRate() override;

	double GetLength() override;

	int PropertiesWindow(HWND hwndParent) override;

	void GetSamples(PCM_source_transfer_t * block) override;

	void GetPeakInfo(PCM_source_peaktransfer_t * block) override;

	void SaveState(ProjectStateContext * ctx) override;

	int LoadState(const char * firstline, ProjectStateContext * ctx) override;

	void Peaks_Clear(bool deleteFile) override;

	int PeaksBuild_Begin() override;

	int PeaksBuild_Run() override;

	void PeaksBuild_Finish() override;

	int Extended(int call, void *parm1, void *parm2, void *parm3) override;

	MD5 getStateHash();
private:
	void renderSID();
	void renderSIDintoMultichannel(String outfn, String outdir);
	void adjustParentTrackChannelCount();
	std::unique_ptr<PCM_source> m_playsource;
	std::mutex m_mutex;
	String m_sidfn;
	String m_displaysidname;
	String m_sidoutfn;
	ChildProcess m_childproc;
	double m_sidlen = 60.0;
	int m_sid_track = 0;
	int m_sid_channelmode = 0;
	int m_sid_sr = 44100;
	MediaItem* m_item = nullptr;
	double m_percent_ready = 0.0;
	ProgressBar m_progbar;
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SID_PCM_Source)
};
