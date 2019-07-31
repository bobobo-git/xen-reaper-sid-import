#define REAPERAPI_IMPLEMENT

#include "reaper_plugin_functions.h"
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <set>
#include "JuceHeader.h"
#include "sid_pcm_source.h"
#include "Commctrl.h"

HINSTANCE g_hInst;
HWND g_parent;

reaper_plugin_info_t* g_plugin_info;

enum toggle_state { CannotToggle, ToggleOff, ToggleOn };

class MySurface : public IReaperControlSurface
{
public:
	const char *GetTypeString() override { return "XENSURFACE"; }
	const char *GetDescString() override { return "Xenakios Control Surface"; }
	const char *GetConfigString() override { return "JUUH"; }

};

IReaperControlSurface *createSurface(const char *type_string, const char *configString, int *errStats)
{
	return new MySurface;
}

class SurfaceConfComponent : public Component
{
public:
	SurfaceConfComponent()
	{

	}
	void paint(Graphics& g) override
	{
		g.fillAll(Colours::black);
		g.setColour(Colours::white);
		g.drawText("foo!!!", 0, 0, getWidth(), getHeight(), Justification::centred);
	}
private:

};

HWND showSurfaceConfig(const char *type_string, HWND parent, const char *initConfigString)
{
	SurfaceConfComponent* comp = new SurfaceConfComponent;
	comp->setSize(100, 100);
	comp->addToDesktop(ComponentPeer::windowIsResizable, (void*)parent);
	
	return (HWND)comp->getPeer();
}

reaper_csurf_reg_t g_surfreg
{ 
	"XENSURFACE",
	"Xenakios Control Surface",
	createSurface,
	showSurfaceConfig 
};

class action_entry
{ //class for registering actions
public:
	action_entry(std::string description, std::string idstring, toggle_state togst, std::function<void(action_entry&)> func);
	action_entry(const action_entry&) = delete; // prevent copying
	action_entry& operator=(const action_entry&) = delete; // prevent copying
	action_entry(action_entry&&) = delete; // prevent moving
	action_entry& operator=(action_entry&&) = delete; // prevent moving

	int m_command_id = 0;
	gaccel_register_t m_accel_reg;
	std::function<void(action_entry&)> m_func;
	std::string m_desc;
	std::string m_id_string;
	int m_val = 0;
	int m_valhw = 0;
	int m_relmode = 0;
	toggle_state m_togglestate = CannotToggle;
	void* m_data = nullptr;
	template<typename T>
	T* getDataAs() { return static_cast<T*>(m_data); }
};


action_entry::action_entry(std::string description, std::string idstring, toggle_state togst, std::function<void(action_entry&)> func) :
	m_desc(description), m_id_string(idstring), m_func(func), m_togglestate(togst)
{
	if (g_plugin_info != nullptr)
	{
		m_accel_reg.accel = { 0,0,0 };
		m_accel_reg.desc = m_desc.c_str();
		m_accel_reg.accel.cmd = m_command_id = g_plugin_info->Register("command_id", (void*)m_id_string.c_str());
		g_plugin_info->Register("gaccel", &m_accel_reg);
	}
}

std::vector<std::shared_ptr<action_entry>> g_actions;

std::shared_ptr<action_entry> add_action(std::string name, std::string id, toggle_state togst, 
	std::function<void(action_entry&)> f)
{
	auto entry = std::make_shared<action_entry>(name, id, togst, f);
	g_actions.push_back(entry);
	return entry;
}

// Reaper calls back to this when it wants to know an actions's toggle state
int toggleActionCallback(int command_id)
{
	for (auto& e : g_actions)
	{
		if (command_id != 0 && e->m_togglestate != CannotToggle && e->m_command_id == command_id)
		{
			if (e->m_togglestate == ToggleOff)
				return 0;
			if (e->m_togglestate == ToggleOn)
				return 1;
		}
	}
	// -1 if action not provided by this extension or is not togglable
	return -1;
}

bool g_juce_messagemanager_inited = false;

class Window : public ResizableWindow
{
public:
	static void initMessageManager()
	{
		if (g_juce_messagemanager_inited == false)
		{
			initialiseJuce_GUI();
			g_juce_messagemanager_inited = true;
		}
	}
	Window(String title, Component* content, int w, int h, bool resizable, Colour bgcolor)
		: ResizableWindow(title,bgcolor,false), m_content_component(content)
	{
		setContentOwned(m_content_component, true);
		setTopLeftPosition(10, 60);
		setSize(w, h);
		setResizable(resizable, false);
		setOpaque(true);
	}
	~Window() 
	{
	}
	int getDesktopWindowStyleFlags() const override
	{
		if (isResizable() == true)
			return ComponentPeer::windowHasCloseButton | ComponentPeer::windowHasTitleBar | ComponentPeer::windowIsResizable | ComponentPeer::windowHasMinimiseButton;
		return ComponentPeer::windowHasCloseButton | ComponentPeer::windowHasTitleBar | ComponentPeer::windowHasMinimiseButton;
	}
	void userTriedToCloseWindow() override
	{
		if (m_assoc_action != nullptr)
		{
			m_assoc_action->m_togglestate = ToggleOff;
			RefreshToolbar(m_assoc_action->m_command_id);
		}
		setVisible(false);
#ifdef WIN32
		BringWindowToTop(GetMainHwnd()); 
#endif
	}
	void visibilityChanged() override
	{
		if (m_assoc_action != nullptr && m_assoc_action->m_togglestate!=CannotToggle)
		{
			if (isVisible() == true)
				m_assoc_action->m_togglestate = ToggleOn;
			else m_assoc_action->m_togglestate = ToggleOff;
		}
		ResizableWindow::visibilityChanged();
	}
	action_entry* m_assoc_action = nullptr;
private:
	Component* m_content_component = nullptr;
	TooltipWindow m_tooltipw;
};

std::unique_ptr<Window> makeWindow(String name, Component* component, int w, int h, bool resizable, Colour backGroundColor)
{
	Window::initMessageManager();
	auto win = std::make_unique<Window>(name, component, w, h, resizable, backGroundColor);
	// This call order is important, the window should not be set visible
	// before adding it into the Reaper window hierarchy
	// Currently this only works for Windows, OS-X needs some really annoying special handling
	// not implemented yet, so just make the window be always on top
#ifdef WIN32
	win->addToDesktop(win->getDesktopWindowStyleFlags(), GetMainHwnd());
#else
	win->addToDesktop(win->getDesktopWindowStyleFlags(), 0);
	win->setAlwaysOnTop(true);
#endif
	return win;
}

bool on_value_action(KbdSectionInfo *sec, int command, int val, int valhw, int relmode, HWND hwnd)
{
	for (auto& e : g_actions)
	{
		if (e->m_command_id != 0 && e->m_command_id == command) {
			e->m_val = val;
			e->m_valhw = valhw;
			e->m_relmode = relmode;
			e->m_func(*e);
			return true;
		}
	}
	return false; // failed to run relevant action
}

PCM_source *CreateFromType(const char *type, int priority)
{
	Window::initMessageManager(); // SetFileName needs timer
	if (priority>4 && strcmp(type, "SIDSOURCE")==0)
		return new SID_PCM_Source;
	return nullptr;
}

PCM_source *CreateFromFile(const char *filename, int priority)
{
	String temp = String(CharPointer_UTF8(filename));
	if (priority > 4 && temp.endsWithIgnoreCase("sid"))
	{
		SID_PCM_Source* src = new SID_PCM_Source;
		src->SetFileName(filename);
		return src;
	}
	return nullptr;
}

const char *EnumFileExtensions(int i, const char **descptr) // call increasing i until returns a string, if descptr's output is NULL, use last description
{
	if (i == 0)
	{
		if (descptr) *descptr = "Commodore64 SID music files";
		return "SID";
	}
	if (descptr) *descptr = nullptr;
	return nullptr;
}

pcmsrc_register_t myRegStruct = { CreateFromType,CreateFromFile,EnumFileExtensions };

inline int intfromvoidptr(void* ptr)
{
	return (int)(int64_t)ptr;
}

inline double floatfromvoidptr(void* ptr)
{
	if (ptr == nullptr)
		return 0.0;
	return *(double*)ptr;
}

class AudioProcessorKeyFrame
{
public:
	AudioProcessorKeyFrame() {}
	AudioProcessorKeyFrame(double tpos, double pitch, double formant, double playrate)
		: m_tpos(tpos), m_pitch(pitch), m_formant(formant), m_playrate(playrate) {}
	double m_tpos = 0.0;
	double m_pitch = 0.0;
	double m_formant = 0.0;
	double m_playrate = 1.0;
};
class XenAudioProcessor;

std::set<XenAudioProcessor*> g_activeprocessors;

class XenAudioProcessor
{
public:
	XenAudioProcessor(MediaItem_Take* sourcetake) : m_take(sourcetake)
	{
		ShowConsoleMsg("XenAudioProcessor created\n");
		g_activeprocessors.insert(this);
	}
	~XenAudioProcessor()
	{
		ShowConsoleMsg("XenAudioProcessor destroyed\n");
		g_activeprocessors.erase(this);
	}
	void addKeyFrame(double tpos, double pitch, double formant, double playrate)
	{
		char buf[1024];
		sprintf(buf, "Adding keyframe %f %f %f %f\n", tpos, pitch, formant, playrate);
		ShowConsoleMsg(buf);
		m_keyframes.emplace_back(tpos, pitch, formant, playrate);
	}
	bool render(char* filename)
	{
		char buf[2048];
		sprintf(buf, "Rendering to file %s\n", filename);
		ShowConsoleMsg(buf);
		return true;
	}
private:
	MediaItem_Take* m_take = nullptr;
	std::vector<AudioProcessorKeyFrame> m_keyframes;
};

void *reascript_XenCreateAudioProcessor(void** args, int numargs)
{
	if (numargs>0)
		return (void*)new XenAudioProcessor((MediaItem_Take*)args[0]);
	return nullptr;
}

void *reascript_XenDestroyAudioProcessor(void** args, int numargs)
{
	if (numargs > 0)
		delete (XenAudioProcessor*)args[0];
	return nullptr;
}

void *reascript_XenAudioProcessorAddKeyFrame(void** args, int numargs)
{
	if (numargs > 4 && args[0] != nullptr)
		((XenAudioProcessor*)args[0])->addKeyFrame(
			floatfromvoidptr(args[1]),
			floatfromvoidptr(args[2]),
			floatfromvoidptr(args[3]),
			floatfromvoidptr(args[4]));
	return nullptr;
}

void *reascript_XenAudioProcessorRender(void** args, int numargs)
{
	bool result = false;
	if (numargs > 1 && args[0] != nullptr && args[1] != nullptr)
	{
		result = ((XenAudioProcessor*)args[0])->render((char*)args[1]);
	}
	return (void*)result;
}

#define FOOBAR2

#ifdef FOOBAR2

extern "C"
{
	REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec) {
		if (rec != nullptr) {
#ifdef WIN32
			Process::setCurrentModuleInstanceHandle(hInstance);
#endif
			if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc) return 0;
			g_hInst = hInstance;
			g_plugin_info = rec;
			g_parent = rec->hwnd_main;
			if (REAPERAPI_LoadAPI(rec->GetFunc) > 0) return 0;
            Window::initMessageManager();
			rec->Register("hookcommand2", (void*)on_value_action);
			rec->Register("toggleaction", (void*)toggleActionCallback);

            rec->Register("pcmsrc", &myRegStruct);

			rec->Register("APIdef_Xen_AudioProcessorCreate", (void*)"XenAudioProcessor*\0MediaItem_Take*\0sourcetake\0");
			rec->Register("APIvararg_Xen_AudioProcessorCreate", (void*)reascript_XenCreateAudioProcessor);

			rec->Register("APIdef_Xen_AudioProcessorDestroy", (void*)"void\0XenAudioProcessor*\0processor\0");
			rec->Register("APIvararg_Xen_AudioProcessorDestroy", (void*)reascript_XenDestroyAudioProcessor);

			rec->Register("APIdef_Xen_AudioProcessorAddKeyFrame", 
				(void*)"void\0XenAudioProcessor*,double,double,double,double\0processor,timepos,pitch,formant,playrate\0");
			rec->Register("APIvararg_Xen_AudioProcessorAddKeyFrame", (void*)reascript_XenAudioProcessorAddKeyFrame);

			rec->Register("APIdef_Xen_AudioProcessorRender", (void*)"bool\0XenAudioProcessor*,char*\0processor,outfilename\0");
			rec->Register("APIvararg_Xen_AudioProcessorRender", (void*)reascript_XenAudioProcessorRender);

			//rec->Register("csurf", (void*)&g_surfreg);

			return 1; // our plugin registered, return success
		}
		else
		{
			if (g_juce_messagemanager_inited == true)
			{
				shutdownJuce_GUI();
				g_juce_messagemanager_inited = false;
			}
			return 0;
		}
	}
};
#else
#define IMPAPI(x) if (!errcnt && !((*(void **)&(x)) = (void *)rec->GetFunc(#x))) errcnt++;
extern "C"
{
	REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec)
	{
		if (rec != nullptr)
		{
			int errcnt = 0;
			IMPAPI(ShowConsoleMsg);
			ShowConsoleMsg("Loading extension plugin\n");
			return 1; // plugin succesfully loaded, return 0 here if could not initialize properly
		}
		else
		{
			// plugin is being unloaded when Reaper quits
			ShowConsoleMsg("Unloading extension plugin\n"); // you will never get to see this message, though...
			return 0;
		}
	}
}
#endif
