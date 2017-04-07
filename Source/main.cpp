#define REAPERAPI_IMPLEMENT

#include "reaper_plugin_functions.h"
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include "JuceHeader.h"
#include "sid_pcm_source.h"

HINSTANCE g_hInst;
HWND g_parent;

reaper_plugin_info_t* g_plugin_info;

enum toggle_state { CannotToggle, ToggleOff, ToggleOn };

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
			Window::initMessageManager();
			e->m_val = val;
			e->m_valhw = valhw;
			e->m_relmode = relmode;
			e->m_func(*e);
			return true;
		}
	}
	return false; // failed to run relevant action
}
#ifdef WIN32
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
		Window::initMessageManager(); // SetFileName needs timer
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
#endif
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

			rec->Register("hookcommand2", (void*)on_value_action);
			rec->Register("toggleaction", (void*)toggleActionCallback);
#ifdef WIN32
            rec->Register("pcmsrc", &myRegStruct);
#endif
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
