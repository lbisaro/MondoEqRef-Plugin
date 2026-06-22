#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "Assets.h"

class MondoEqRefApplication : public juce::JUCEApplication
{
public:
    MondoEqRefApplication() {}
    const juce::String getApplicationName() override       { return "MondoEqRef"; }
    const juce::String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    std::unique_ptr<juce::SplashScreen> splash;

    void initialise (const juce::String& commandLine) override
    {
        juce::File logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MondoEqRef");
        logDir.createDirectory();
        logger.reset(juce::FileLogger::createDateStampedLogger(logDir.getFullPathName(), "debug_", ".log", "MondoEqRef Debug"));
        juce::Logger::setCurrentLogger(logger.get());

        juce::Image icon = juce::ImageCache::getFromMemory(Assets::icon_png, Assets::icon_pngSize);
        if (icon.isValid())
        {
            // Scale icon to max 150px
            if (icon.getWidth() > 150)
            {
                float ratio = 150.0f / (float)icon.getWidth();
                icon = icon.rescaled(150, (int)(icon.getHeight() * ratio), juce::Graphics::highResamplingQuality);
            }
            splash.reset(new juce::SplashScreen("MondoEqRef", icon, true));
        }

        juce::MessageManager::getInstance()->callAsync([this]() {
            try {
                mainWindow.reset (new MainWindow (getApplicationName()));
            } catch (const std::exception& e) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Startup Error", e.what());
                quit();
            } catch (...) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Startup Error", "Unknown Error");
                quit();
            }
            if (splash != nullptr)
                splash->deleteAfterDelay(juce::RelativeTime::seconds(2.0), false);
        });
    }

    void shutdown() override
    {
        splash = nullptr;
        mainWindow = nullptr;
        juce::Logger::setCurrentLogger(nullptr);
        logger = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
    }

    std::unique_ptr<juce::Logger> logger;

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MondoEqRefApplication)
