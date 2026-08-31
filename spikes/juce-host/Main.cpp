#include <juce_gui_extra/juce_gui_extra.h>

class MainWindow final : public juce::DocumentWindow {
 public:
  explicit MainWindow(juce::String name)
      : juce::DocumentWindow(std::move(name), juce::Colours::black,
                             juce::DocumentWindow::allButtons) {
    auto* label = new juce::Label();
    label->setText("AudioMasteringTool JUCE 8 host feasibility spike", juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);
    setContentOwned(label, true);
    centreWithSize(900, 560);
    setVisible(true);
  }
  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class SpikeApplication final : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return "AMT JUCE 8 Host Spike"; }
  const juce::String getApplicationVersion() override { return "0.0.1"; }
  void initialise(const juce::String&) override { window = std::make_unique<MainWindow>(getApplicationName()); }
  void shutdown() override { window.reset(); }
 private:
  std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(SpikeApplication)
