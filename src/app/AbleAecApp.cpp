// AbleAEC as a desktop application (the JUCE "Standalone" format with a custom application class).
//
//  - Audio comes from / goes to the audio device chosen in Audio settings. The mic and the speaker
//    reference arrive on input channels (1-2 + 3-4, or 1 + 2); the cleaned mic goes to the output.
//  - Lives in the system tray: closing the window only hides it.
//  - "Start at login" registers the app to start hidden (--minimized) when the user logs in.
//  - MIDI input devices are opened automatically; keyboard shortcuts are handled by the editor.

#include "../plugin/PluginProcessor.h"
#include "../plugin/UiStrings.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#if JucePlugin_Build_Standalone

namespace
{
const juce::Colour kBg { 0xff1e2126 };
const juce::Colour kAccent { 0xffffa31a };
const juce::Colour kText { 0xffe6e6e6 };

//==============================================================================
namespace autostart
{
juce::String launchCommand()
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_MAC
    // inside AbleAEC.app/Contents/MacOS
   #endif
    return exe.getFullPathName();
}

#if JUCE_WINDOWS
const juce::String runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\AbleAEC";
bool isEnabled() { return juce::WindowsRegistry::valueExists (runKey); }
void setEnabled (bool on)
{
    if (on)
        juce::WindowsRegistry::setValue (runKey, "\"" + launchCommand() + "\" --minimized");
    else
        juce::WindowsRegistry::deleteValue (runKey);
}
#elif JUCE_MAC
juce::File agentFile()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Library/LaunchAgents/com.ableaec.ableaec.plist");
}
bool isEnabled() { return agentFile().existsAsFile(); }
void setEnabled (bool on)
{
    if (! on) { agentFile().deleteFile(); return; }
    agentFile().getParentDirectory().createDirectory();
    agentFile().replaceWithText (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>com.ableaec.ableaec</string>\n"
        "  <key>ProgramArguments</key><array><string>" + launchCommand() + "</string><string>--minimized</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "</dict></plist>\n");
}
#else
juce::File desktopFile()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config/autostart/ableaec.desktop");
}
bool isEnabled() { return desktopFile().existsAsFile(); }
void setEnabled (bool on)
{
    if (! on) { desktopFile().deleteFile(); return; }
    desktopFile().getParentDirectory().createDirectory();
    desktopFile().replaceWithText ("[Desktop Entry]\nType=Application\nName=AbleAEC\nExec=\"" + launchCommand()
                                   + "\" --minimized\nX-GNOME-Autostart-enabled=true\n");
}
#endif
} // namespace autostart

juce::Image trayImage()
{
    juce::Image img (juce::Image::ARGB, 64, 64, true);
    juce::Graphics g (img);
    g.setColour (kAccent);
    g.fillEllipse (4.0f, 4.0f, 56.0f, 56.0f);
    g.setColour (kBg);
    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    g.drawText ("A", img.getBounds(), juce::Justification::centred);
    return img;
}

//==============================================================================
// Top bar (audio settings, input layout, autostart) above the plugin's own editor.
class AppContent final : public juce::Component, private juce::Timer
{
public:
    explicit AppContent (juce::StandalonePluginHolder& h) : holder (h)
    {
        proc = dynamic_cast<AbleAecProcessor*> (holder.processor.get());
        editor.reset (holder.processor->createEditorIfNeeded());
        addAndMakeVisible (*editor);

        settingsButton.onClick = [this] { holder.showAudioSettingsDialog(); };
        addAndMakeVisible (settingsButton);

        routingBox.onChange = [this]
        {
            if (proc != nullptr)
                proc->setInputRouting ((AbleAecProcessor::InputRouting) (routingBox.getSelectedId() - 1));
        };
        addAndMakeVisible (routingBox);
        addAndMakeVisible (routingLabel);

        autostartButton.setToggleState (autostart::isEnabled(), juce::dontSendNotification);
        autostartButton.onClick = [this] { autostart::setEnabled (autostartButton.getToggleState()); };
        autostartButton.setColour (juce::ToggleButton::textColourId, kText);
        autostartButton.setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (autostartButton);

        updateTexts();
        setSize (editor->getWidth(), editor->getHeight() + barHeight);
        startTimer (1000);
    }

    ~AppContent() override
    {
        if (editor != nullptr)
            holder.processor->editorBeingDeleted (editor.get());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBg.darker (0.3f));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight).reduced (12, 8);
        settingsButton.setBounds (bar.removeFromLeft (120));
        bar.removeFromLeft (16);
        routingLabel.setBounds (bar.removeFromLeft (70));
        routingBox.setBounds (bar.removeFromLeft (190));
        autostartButton.setBounds (bar.removeFromRight (190));
        editor->setBounds (r);
    }

    juce::AudioProcessorEditor* getEditor() const { return editor.get(); }

private:
    void timerCallback() override { updateTexts(); }

    void updateTexts()
    {
        const auto l = proc != nullptr ? proc->getUiLanguage() : ui::Lang::en;
        if (l == shownLang && routingBox.getNumItems() > 0)
            return;
        shownLang = l;
        settingsButton.setButtonText (ui::tr (ui::Str::appAudioSettings, l));
        routingLabel.setText (ui::tr (ui::Str::appInputs, l), juce::dontSendNotification);
        routingLabel.setColour (juce::Label::textColourId, kText);
        routingBox.clear (juce::dontSendNotification);
        routingBox.addItem (ui::tr (ui::Str::appRoute4, l), 1);
        routingBox.addItem (ui::tr (ui::Str::appRoute2, l), 2);
        routingBox.setSelectedId (proc != nullptr ? (int) proc->getInputRouting() + 1 : 1, juce::dontSendNotification);
        routingBox.setTooltip (ui::tr (ui::Str::tipAppInputs, l));
        autostartButton.setButtonText (ui::tr (ui::Str::appAutostart, l));
        autostartButton.setTooltip (ui::tr (ui::Str::tipAutostart, l) + "  " + ui::tr (ui::Str::appTrayNote, l));
    }

    static constexpr int barHeight = 44;
    juce::StandalonePluginHolder& holder;
    AbleAecProcessor* proc = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::TextButton settingsButton;
    juce::Label routingLabel;
    juce::ComboBox routingBox;
    juce::ToggleButton autostartButton;
    ui::Lang shownLang = ui::Lang::en;
};

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (juce::StandalonePluginHolder& holder, std::function<void()> onClose)
        : DocumentWindow ("AbleAEC", kBg, DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          closeHandler (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        auto* content = new AppContent (holder);
        setContentOwned (content, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
    }

    void closeButtonPressed() override { closeHandler(); }

private:
    std::function<void()> closeHandler;
};

//==============================================================================
class Tray final : public juce::SystemTrayIconComponent
{
public:
    Tray (std::function<void()> show, std::function<void()> settings, std::function<void()> quit, std::function<ui::Lang()> lang)
        : onShow (std::move (show)), onSettings (std::move (settings)), onQuit (std::move (quit)), getLang (std::move (lang))
    {
        setIconImage (trayImage(), trayImage());
        setIconTooltip ("AbleAEC");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
       #if ! JUCE_MAC
        if (e.mods.isLeftButtonDown() && ! e.mods.isPopupMenu()) // left click on Windows / Linux opens the window
        {
            onShow();
            return;
        }
       #endif
        juce::ignoreUnused (e);
        const auto l = getLang();
        juce::PopupMenu menu;
        menu.addItem (1, ui::tr (ui::Str::trayOpen, l));
        menu.addItem (2, ui::tr (ui::Str::appAudioSettings, l));
        menu.addItem (3, ui::tr (ui::Str::appAutostart, l), true, autostart::isEnabled());
        menu.addSeparator();
        menu.addItem (4, ui::tr (ui::Str::trayQuit, l));
        menu.showMenuAsync (juce::PopupMenu::Options(), [this] (int result)
        {
            if (result == 1) onShow();
            else if (result == 2) onSettings();
            else if (result == 3) autostart::setEnabled (! autostart::isEnabled());
            else if (result == 4) onQuit();
        });
    }

private:
    std::function<void()> onShow, onSettings, onQuit;
    std::function<ui::Lang()> getLang;
};
} // namespace

//==============================================================================
class AbleAecApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #endif
        appProperties.setStorageParameters (options);

        holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String(),
                                                                 nullptr, juce::Array<juce::StandalonePluginHolder::PluginInOuts>(),
                                                                 true /* open MIDI inputs */);
        // The standalone wrapper mutes the input by default (feedback protection); this app needs the mic.
        holder->getMuteInputValue().setValue (false);

        tray = std::make_unique<Tray> ([this] { showWindow(); },
                                       [this] { holder->showAudioSettingsDialog(); },
                                       [this] { systemRequestedQuit(); },
                                       [this] { return uiLanguage(); });

        window = std::make_unique<MainWindow> (*holder, [this] { hideWindow(); });
        if (! commandLine.contains ("--minimized"))
            showWindow();
    }

    void shutdown() override
    {
        if (holder != nullptr)
            holder->savePluginState();
        window = nullptr;
        tray = nullptr;
        holder = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (holder != nullptr)
            holder->savePluginState();
        quit();
    }

private:
    ui::Lang uiLanguage() const
    {
        if (auto* p = holder != nullptr ? dynamic_cast<AbleAecProcessor*> (holder->processor.get()) : nullptr)
            return p->getUiLanguage();
        return ui::Lang::en;
    }

    void showWindow()
    {
        if (window == nullptr) return;
        window->setVisible (true);
        window->setMinimised (false);
        window->toFront (true);
        if (auto* content = dynamic_cast<AppContent*> (window->getContentComponent()))
            if (auto* ed = content->getEditor())
                ed->grabKeyboardFocus(); // shortcuts work right away
    }

    void hideWindow()
    {
        if (holder != nullptr)
            holder->savePluginState();
        window->setVisible (false); // keeps running in the tray
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<Tray> tray;
};

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new AbleAecApp(); }

#endif
