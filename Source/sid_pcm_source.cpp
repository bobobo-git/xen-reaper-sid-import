#include "sid_pcm_source.h"
#include "lineparse.h"
#include <array>

int g_render_tasks = 0;

SID_PCM_Source::SID_PCM_Source() : m_progbar(m_percent_ready)
{
	if (HasExtState("sid_import", "default_len"))
		m_sidlen = jlimit(1.0, 600.0, atof(GetExtState("sid_import", "default_len")));
	if (HasExtState("sid_import", "default_sr"))
		m_sid_sr = jlimit(8000.0, 384000.0, atof(GetExtState("sid_import", "default_sr")));
}

SID_PCM_Source::~SID_PCM_Source()
{
	if (m_childproc.isRunning() == true)
	{
		stopTimer(1);
		//ShowConsoleMsg("SID_PCM_Source destroyed while rendering\n");
		if (m_childproc.kill() == true)
		{
			//ShowConsoleMsg("Child process killed\n");
			File temp(m_sidoutfn);
			if (temp.deleteFile() == false)
			{
				ShowConsoleMsg("Could not delete file!\n");
			}
			else
			{
				//ShowConsoleMsg("File deleted\n");
			}
		}
		else ShowConsoleMsg("Could not kill rendering process!\n");
	}
}

void SID_PCM_Source::timerCallback(int id)
{
	if (id == 0)
	{
		stopTimer(0);
		renderSID();
	}
	if (id == 1)
	{
		if (m_childproc.isRunning() == false)
		{
			m_progbar.removeFromDesktop();
			--g_render_tasks;
			if (g_render_tasks < 0)
				g_render_tasks = 0;
			stopTimer(1);
			if (m_childproc.getExitCode() == 0)
			{
				PCM_source* src = PCM_Source_CreateFromFile(m_sidoutfn.toRawUTF8());
				if (src != nullptr)
				{
					m_mutex.lock();
					m_playsource = std::unique_ptr<PCM_source>(src);
					m_mutex.unlock();
					Main_OnCommand(40047, 0); // build any missing peaks
				}
				else ShowConsoleMsg("Could not create pcm_source\n");
			}
			else
			{
				String temp = m_childproc.readAllProcessOutput();
				AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SID import error", temp);
			}
		}
		else
		{
			File temp(m_sidoutfn);
			double percent = 1.0 / (m_sidlen*m_sid_sr * 2)*temp.getSize();
			m_percent_ready = percent;
			//String msg = String(percent, 1) + "% ready...\n";
			//ShowConsoleMsg(msg.toRawUTF8());
		}
	}
}


PCM_source * SID_PCM_Source::Duplicate()
{
	SID_PCM_Source* dupl = new SID_PCM_Source;
	dupl->m_sidfn = m_sidfn;
	dupl->m_sidlen = m_sidlen;
	dupl->m_sid_track = m_sid_track;
	dupl->m_sid_channelmode = m_sid_channelmode;
	dupl->m_sid_sr = m_sid_sr;
	dupl->m_sid_render_mode = m_sid_render_mode;
	dupl->renderSID();
	return dupl;
}

bool SID_PCM_Source::IsAvailable()
{
	return m_playsource != nullptr;
}

const char * SID_PCM_Source::GetType()
{
	return "SIDSOURCE";
}

bool SID_PCM_Source::SetFileName(const char * newfn)
{
	stopTimer(0);
	String temp = String(CharPointer_UTF8(newfn));
	if (temp != m_sidfn && temp.endsWithIgnoreCase("sid"))
	{
		m_sidfn = String(CharPointer_UTF8(newfn));
		m_displaysidname = m_sidfn;
		/*
			The rendering is done delayed with a timer so that the rendering doesn't slow down Reaper operation when
			it creates a temporary instance of our class, for example when a file is drag and dropped into Reaper.
		*/
		startTimer(0, 500);
		return true;
	}
	return false;
}

const char * SID_PCM_Source::GetFileName()
{
	return m_displaysidname.toRawUTF8();
}

int SID_PCM_Source::GetNumChannels()
{
	if (m_playsource == nullptr)
		return 1;
	return m_playsource->GetNumChannels();
}

double SID_PCM_Source::GetSampleRate()
{
	return m_sid_sr;
}

double SID_PCM_Source::GetLength()
{
	if (m_playsource == nullptr)
		return m_sidlen;
	return m_playsource->GetLength();
}

int SID_PCM_Source::PropertiesWindow(HWND hwndParent)
{
	juce::AlertWindow aw("SID source properties",m_sidfn,AlertWindow::InfoIcon);
	LookAndFeel_V3 lookandfeel;
	aw.setLookAndFeel(&lookandfeel);
	StringArray items;
	items.add("Default track");
	for (int i = 1; i < 21; ++i)
		items.add(String(i));
	aw.addComboBox("tracknum", items, "Track number");
	aw.getComboBoxComponent("tracknum")->setSelectedId(m_sid_track+1);
	
	items.clear();
	items.add("All channels (mono)");
	items.add("All channels (3 channels)");
	for (int i = 1; i < 5; ++i)
		items.add("Solo "+String(i));
	aw.addComboBox("channelmode", items, "Channel mode");
	if (m_sid_channelmode == 0)
		aw.getComboBoxComponent("channelmode")->setSelectedId(1);
	if (m_sid_channelmode == 10)
		aw.getComboBoxComponent("channelmode")->setSelectedId(2);
	if (m_sid_channelmode >= 1 && m_sid_channelmode < 5)
		aw.getComboBoxComponent("channelmode")->setSelectedId(m_sid_channelmode + 2);
	items.clear();
	items.add("Slow render");
	items.add("Faster render");
	aw.addComboBox("rendermode", items, "Render mode");
	aw.getComboBoxComponent("rendermode")->setSelectedId(m_sid_render_mode + 1);
	aw.addTextEditor("sr", String(m_sid_sr), "Samplerate");
	aw.addTextEditor("tracklen", String(m_sidlen, 1), "Length to use");
	aw.addButton("Cancel", 3);
	aw.addButton("OK and use as defaults", 2);
	aw.addButton("OK", 1);
	
	aw.setSize(aw.getWidth() + 200, aw.getHeight());
	aw.setTopLeftPosition(aw.getX() - 100, aw.getY());
	int r = aw.runModalLoop();
	if (r == 1 || r == 2)
	{
		m_sid_track = aw.getComboBoxComponent("tracknum")->getSelectedId()-1;
		double len = aw.getTextEditorContents("tracklen").getDoubleValue();
		m_sidlen = jlimit(1.0, 1200.0, len);
		int chanmode = aw.getComboBoxComponent("channelmode")->getSelectedId();
		if (chanmode == 1)
			m_sid_channelmode = 0;
		if (chanmode == 2)
			m_sid_channelmode = 10;
		if (chanmode >=3 && chanmode<=6)
			m_sid_channelmode = chanmode - 2;
		m_sid_sr = jlimit(8000, 384000, aw.getTextEditorContents("sr").getIntValue());
		m_sid_render_mode = jlimit(0, 1, aw.getComboBoxComponent("rendermode")->getSelectedId() - 1);
		if (r == 2)
		{
			char tempbuf[100];
			sprintf(tempbuf, "%f", m_sidlen);
			SetExtState("sid_import", "default_len", tempbuf, true);
			sprintf(tempbuf, "%d", m_sid_sr);
			SetExtState("sid_import", "default_sr", tempbuf, true);
		}
		renderSID();
	}
	return 0;
}

void SID_PCM_Source::GetSamples(PCM_source_transfer_t * block)
{
	std::lock_guard<std::mutex> locker(m_mutex);
	if (m_playsource == nullptr)
		return;
	m_playsource->GetSamples(block);
}

void SID_PCM_Source::GetPeakInfo(PCM_source_peaktransfer_t * block)
{
	if (m_playsource == nullptr)
		return;
	m_playsource->GetPeakInfo(block);
}

void SID_PCM_Source::SaveState(ProjectStateContext * ctx)
{
	ctx->AddLine("FILE \"%s\" %f %d %d %d %d", m_sidfn.toRawUTF8(),m_sidlen,m_sid_track, m_sid_channelmode,m_sid_sr,m_sid_render_mode);
}

int SID_PCM_Source::LoadState(const char * firstline, ProjectStateContext * ctx)
{
	LineParser lp;
	char buf[2048];
	for (;;)
	{
		if (ctx->GetLine(buf, sizeof(buf))) 
			break;
		lp.parse(buf);
		if (strcmp(lp.gettoken_str(0), "FILE") == 0)
		{
			m_sidfn = String(CharPointer_UTF8(lp.gettoken_str(1)));
			m_sidlen = lp.gettoken_float(2);
			m_sid_track = lp.gettoken_int(3);
			m_sid_channelmode = lp.gettoken_int(4);
			m_sid_sr = lp.gettoken_int(5);
			m_sid_render_mode = lp.gettoken_int(6);
		}
		if (lp.gettoken_str(0)[0] == '>')
		{
			renderSID();
			return 0;
		}
	}
	return -1;
}

void SID_PCM_Source::Peaks_Clear(bool deleteFile)
{
	if (m_playsource == nullptr)
		return;
	m_playsource->Peaks_Clear(deleteFile);
}

int SID_PCM_Source::PeaksBuild_Begin()
{
	if (m_playsource == nullptr)
		return 0;
	return m_playsource->PeaksBuild_Begin();
}

int SID_PCM_Source::PeaksBuild_Run()
{
	if (m_playsource == nullptr)
		return 0;
	return m_playsource->PeaksBuild_Run();
}

void SID_PCM_Source::PeaksBuild_Finish()
{
	if (m_playsource == nullptr)
		return;
	m_playsource->PeaksBuild_Finish();
}

int SID_PCM_Source::Extended(int call, void * parm1, void * parm2, void * parm3)
{
	if (call == PCM_SOURCE_EXT_ENDPLAYNOTIFY)
	{
		//char buf[500];
		//sprintf(buf, "Play ended for SID source %p\n", (void*)this);
		//ShowConsoleMsg(buf);
	}
	if (call == PCM_SOURCE_EXT_SETITEMCONTEXT)
	{
		m_item = (MediaItem*)parm1;
	}
	return 0;
}

MD5 SID_PCM_Source::getStateHash()
{
	MemoryBlock mb;
	mb.append(m_sidfn.toRawUTF8(), m_sidfn.getNumBytesAsUTF8());
	mb.append(&m_sidlen, sizeof(double));
	mb.append(&m_sid_channelmode, sizeof(int));
	mb.append(&m_sid_track, sizeof(int));
	mb.append(&m_sid_sr, sizeof(int));
	mb.append(&m_sid_render_mode, sizeof(int));
	return MD5(mb);
}

void SID_PCM_Source::renderSID()
{
	if (m_sidfn.isEmpty() == true)
		return;
	auto hash = getStateHash();
	const char* rscpath = GetResourcePath();
	String outfolder = String(CharPointer_UTF8(rscpath)) + "/UserPlugins/sid_cache";
	File diskfolder(outfolder);
	if (diskfolder.exists() == false)
	{
		diskfolder.createDirectory();
	}
	String outfn = outfolder+"/" + hash.toHexString() + ".wav";
	File temp(outfn);
	if (temp.existsAsFile() == true)
	{
		PCM_source* src = PCM_Source_CreateFromFile(outfn.toRawUTF8());
		if (src != nullptr)
		{
			m_mutex.lock();
			m_playsource = std::unique_ptr<PCM_source>(src);
			m_mutex.unlock();
			if (m_playsource->GetNumChannels() > 2)
				adjustParentTrackChannelCount();
			Main_OnCommand(40047, 0); // build any missing peaks
			return;
		}
	}
	if (m_sid_channelmode == 10)
	{
		renderSIDintoMultichannel(outfn, outfolder);
		return;
	}
	StringArray args;
#ifdef USE_SID2WAV
	String exename = String(CharPointer_UTF8(GetResourcePath())) + "/UserPlugins/SID2WAV.EXE";
	args.add(exename);
	args.add("-t" + String(m_sidlen));
	args.add("-16");
	args.add("-f" + String(m_sid_sr));
	if (m_sid_track > 0)
		args.add("-o" + String(m_sid_track));
	if (m_sid_channelmode > 0)
	{
		String chanarg("-m");
		for (int i = 1; i < 5; ++i)
			if (i != m_sid_channelmode)
				chanarg.append(String(i),1);
		args.add(chanarg);
	}
	args.add(m_sidfn);
	args.add(outfn);
#else
	String exename = String(CharPointer_UTF8(GetResourcePath())) + "/UserPlugins/sidplayfp.exe";
	args.add(exename);
	args.add("-t" + String((int)m_sidlen));
	//args.add("-16");
	args.add("-f" + String(m_sid_sr));
	if (m_sid_track > 0)
		args.add("-o" + String(m_sid_track));
	if (m_sid_channelmode > 0)
	{
		String chanarg("-m");
		for (int i = 1; i < 5; ++i)
			if (i != m_sid_channelmode)
				chanarg.append(String(i), 1);
		args.add(chanarg);
	}
	args.add("-w"+outfn);
	args.add("-q"); // text output not needed
	if (m_sid_render_mode == 1)
	{
		args.add("--resid");
		args.add("-rif"); // fast resid resample
	}
	args.add(m_sidfn);
#endif
	m_sidoutfn = outfn;
	m_childproc.start(args);
	m_percent_ready = 0.0;
	m_progbar.addToDesktop(0, 0);
	m_progbar.setVisible(true);
	m_progbar.setBounds(10, 60+30*g_render_tasks, 500, 25);
	m_progbar.setAlwaysOnTop(true);
	m_progbar.setColour(ProgressBar::foregroundColourId, Colours::white);
	m_progbar.setColour(ProgressBar::backgroundColourId, Colours::lightgrey);
	//m_progbar.setTextToDisplay("Rendering SID...");
	++g_render_tasks;
	startTimer(1, 1000);
}

/*
OK, so this is a monstrous function...Could maybe be refactored a bit, but not much.
What happens here is that SID2WAV is called to render each channel of the SID tune into a separate temporary WAV file.
Optionally spawns the SID2WAV processes at the same time to process them in parallel.
Those files are then rendered into a single multichannel file that will be used for playback.
*/

void SID_PCM_Source::renderSIDintoMultichannel(String outfn, String outdir)
{
	double t0 = Time::getMillisecondCounterHiRes();
	int numoutchans = 3;
	bool do_parallel_render = true;
	ChildProcess childprocs[4];
	for (int i = 0; i < numoutchans; ++i)
	{
		StringArray args;
		String exename = String(CharPointer_UTF8(GetResourcePath())) + "/UserPlugins/SID2WAV.EXE";
		args.add(exename);
		args.add("-t" + String(m_sidlen));
		args.add("-16");
		args.add("-f" + String(m_sid_sr));
		if (m_sid_track > 0)
			args.add("-o" + String(m_sid_track));
		
		String chanarg("-m");
		for (int j = 1; j < 5; ++j)
			if (j != i+1)
				chanarg.append(String(j), 1);
		args.add(chanarg);
		args.add(m_sidfn);
		String chantempname = outdir + "/sidtemp" + String(i) + ".wav";
		args.add(chantempname);
		if (do_parallel_render == true)
		{
			childprocs[i].start(args);
		}
		else
		{
			childprocs[0].start(args);
			childprocs[0].waitForProcessToFinish(60000);
		}
	}
	bool rendered_ok = false;
	if (do_parallel_render == true)
	{
		bool cancel = false;
		while (true)
		{
			int ready_count = 0;
			for (int i = 0; i < numoutchans; ++i)
			{
				if (childprocs[i].isRunning() == false)
				{
					if (childprocs[i].getExitCode() == 0)
						++ready_count;
					else
					{
						cancel = true;
					}
				}
			}
			if (cancel == true)
			{
				break;
			}
			if (ready_count >= numoutchans)
			{
				rendered_ok = true;
				break;
			}
			Thread::sleep(50);
		}
	}
	else
		rendered_ok = childprocs[0].getExitCode() == 0;
	if (rendered_ok == true)
	{
		char cfg[] = { 'e','v','a','w', 16, 0 };
		auto sink = std::unique_ptr<PCM_sink>(PCM_Sink_Create(outfn.toRawUTF8(),
			cfg, sizeof(cfg), numoutchans, m_sid_sr, true));
		std::array<std::unique_ptr<PCM_source>, 4> sources;
		for (int i = 0; i < numoutchans; ++i)
		{
			String chantempname = outdir + "/sidtemp" + String(i) + ".wav";
			PCM_source* src = PCM_Source_CreateFromFile(chantempname.toRawUTF8());
			sources[i] = std::unique_ptr<PCM_source>(src);
		}
		int bufsize = 262144;
		std::vector<double> srcbuf(bufsize);
		std::vector<double> sinkbuf(bufsize*numoutchans);
		std::vector<double*> sinkbufptrs(numoutchans);
		for (int i = 0; i < numoutchans; ++i)
			sinkbufptrs[i] = &sinkbuf[i*bufsize];
		int outcounter = 0;
		int outlenframes = sources[0]->GetLength()*m_sid_sr;
		while (outcounter < outlenframes)
		{
			int framestoread = std::min<int64_t>(bufsize, outlenframes - outcounter);
			for (int i = 0; i < numoutchans; ++i)
			{
				PCM_source_transfer_t transfer = { 0 };
				transfer.nch = 1;
				transfer.length = bufsize;
				transfer.samplerate = m_sid_sr;
				transfer.time_s = (double)outcounter / m_sid_sr;
				transfer.samples = srcbuf.data();
				sources[i]->GetSamples(&transfer);
				for (int j = 0; j < bufsize; ++j)
					sinkbufptrs[i][j] = srcbuf[j];
			}
			sink->WriteDoubles(sinkbufptrs.data(), framestoread, numoutchans, 0, 1);
			outcounter += bufsize;
		}
		sink = nullptr;
		PCM_source* src = PCM_Source_CreateFromFile(outfn.toRawUTF8());
		if (src != nullptr)
		{
			m_mutex.lock();
			m_playsource = std::unique_ptr<PCM_source>(src);
			m_mutex.unlock();
			adjustParentTrackChannelCount();
			Main_OnCommand(40047, 0); // build any missing peaks
		}
		for (int i = 0; i < numoutchans; ++i)
		{
			sources[i] = nullptr;
			String chantempname = outdir + "/sidtemp" + String(i) + ".wav";
			File temp(chantempname);
			temp.deleteFile();
		}
		double t1 = Time::getMillisecondCounterHiRes();
		if (do_parallel_render == false)
			Logger::writeToLog("with non-parallel SID render took " + String(t1 - t0) + " ms");
		else Logger::writeToLog("with parallel SID render took " + String(t1 - t0) + " ms");
	}
	else
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "SID import error", "Could not create channel temporary files");
	}
}

void SID_PCM_Source::adjustParentTrackChannelCount()
{
	if (m_item != nullptr)
	{
		MediaTrack* track = GetMediaItemTrack(m_item);
		if (track != nullptr)
		{
			int num = 4;
			GetSetMediaTrackInfo(track, "I_NCHAN", &num);
		}
	}
}
