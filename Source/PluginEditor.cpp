#include "PluginEditor.h"

namespace t2m
{
using LnF = T2MLookAndFeel;

namespace
{
std::unique_ptr<juce::PropertiesFile> openPrefs()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Talk2Me";
    o.filenameSuffix = "settings";
    o.folderName = "Talk2Me";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}

const char* playDescription (int m)
{
    switch (m)
    {
        case 1:  return "Each note sings the next syllable and holds its vowel while the key is down.";
        case 2:  return "Each note speaks the next word and holds its stressed vowel while the key is down.";
        case 3:  return "Each key from C3 up speaks its own word of the phrase, like a sampler.";
        default: return "Each note speaks the whole phrase at its pitch. Overlapping notes glide without restarting.";
    }
}
} // namespace

Talk2MeEditor::Talk2MeEditor (Talk2MeProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      transpose (p.apvts), outMeter (p.apvts), phrase (p),
      voiceChoice (p.apvts, ParamId::voice, "Voice"), chipChoice (p.apvts, ParamId::chip, "Chip")
{
    Accessibility::refresh();
    auto prefs = openPrefs();
    themeMode = (ThemeMode) juce::jlimit (0, 2, prefs->getIntValue ("themeMode", 0));
    applyThemeMode();
    setLookAndFeel (&lnf);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    setTitle ("Talk2Me");
    addAndMakeVisible (content);

    // тулбар
    for (juce::Component* c : std::initializer_list<juce::Component*> { &logo, &prevPreset, &presetName, &nextPreset, &engineSwitch, &mini, &transpose, &outMeter, &settingsButton })
        content.addAndMakeVisible (c);
    logo.onClick = [this] { showAbout (true); };
    presetName.setTitle ("Preset");
    presetName.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { proc.presets.step (-1); };
    nextPreset.onClick = [this] { proc.presets.step (1); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    engineSwitch.setTitle ("Engine");
    engineSwitch.setItems ({ { "Talk 96", 0 }, { "Spell 80", 1 } });
    engineAtt = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamId::engine),
        [this] (float v) { engineSwitch.setSelectedValue ((int) v); }, nullptr);
    engineSwitch.onChange = [this] (int v) { engineAtt->setValueAsCompleteGesture ((float) v); };
    engineAtt->sendInitialUpdate();

    content.addAndMakeVisible (phrase);
    content.addAndMakeVisible (scriptView);

    // панель
    playSwitch.setTitle ("Play mode");
    playSwitch.setItems ({ { "Phrase", 0 }, { "Syllables", 1 }, { "Words", 2 }, { "Keys", 3 } });
    playAtt = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamId::play),
        [this] (float v) { playSwitch.setSelectedValue ((int) v); }, nullptr);
    playSwitch.onChange = [this] (int v) { playAtt->setValueAsCompleteGesture ((float) v); };
    playAtt->sendInitialUpdate();
    content.addAndMakeVisible (playSwitch);

    rate.attach (p.apvts, ParamId::rate);
    intonation.attach (p.apvts, ParamId::intonation);
    glide.attach (p.apvts, ParamId::glide);
    vibrato.attach (p.apvts, ParamId::vibrato);
    release.attach (p.apvts, ParamId::release);
    formant.attach (p.apvts, ParamId::formant);
    breath.attach (p.apvts, ParamId::breath);
    buzz.attach (p.apvts, ParamId::buzz);
    lofi.attach (p.apvts, ParamId::lofi);
    bend.attach (p.apvts, ParamId::bend);
    drive.attach (p.apvts, ParamId::drive);
    glitch.attach (p.apvts, ParamId::glitch);
    lofi2.attach (p.apvts, ParamId::lofi);
    poly.attach (p.apvts, ParamId::poly);
    loop.attach (p.apvts, ParamId::loop);
    chipPitch.attach (p.apvts, ParamId::chipPitch);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &rate, &intonation, &glide, &vibrato, &release, &voiceChoice, &formant, &breath, &buzz, &lofi,
                                                                        &chipChoice, &bend, &drive, &glitch, &lofi2, &poly, &loop, &chipPitch, &status })
        content.addAndMakeVisible (c);
    status.setFont (LnF::mono (LnF::Type::footnote));
    status.getProperties().set ("dim", true);
    status.setJustificationType (juce::Justification::centredRight);

    content.addChildComponent (about);
    about.onClose = [this] { showAbout (false); };
    disableClickFocus (*this);

    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / kBaseH);
    const int scale = juce::jlimit (50, 200, prefs->getIntValue ("scale", 100));
    setSize (kBaseW * scale / 100, kBaseH * scale / 100);
    updateEngineControls();
    timerCallback();
    startTimerHz (30);
}

Talk2MeEditor::~Talk2MeEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void Talk2MeEditor::applyThemeMode()
{
    ThemeId id = ThemeId::Oxygen;
    if (themeMode == ThemeMode::Rubber) id = ThemeId::Rubber;
    else if (themeMode == ThemeMode::System) id = juce::Desktop::getInstance().isDarkModeActive() ? ThemeId::Rubber : ThemeId::Oxygen;
    applyTheme (id);
    lnf.refreshColours();
    sendLookAndFeelChange();
    repaintTree (*this);
}

void Talk2MeEditor::setThemeMode (ThemeMode m)
{
    themeMode = m;
    if (auto prefs = openPrefs()) { prefs->setValue ("themeMode", (int) m); prefs->saveIfNeeded(); }
    applyThemeMode();
}

void Talk2MeEditor::darkModeSettingChanged()
{
    if (themeMode == ThemeMode::System) applyThemeMode();
}

void Talk2MeEditor::showAbout (bool show)
{
    if (show == about.isVisible()) return;
    if (show)
    {
        about.setBounds (0, 0, kBaseW, kBaseH);
        about.setBackLogoBounds (logo.getBounds());
        about.setVisible (true);
        about.toFront (true);
        about.grabKeyboardFocus();
    }
    else
    {
        about.showGame (false);
        about.setVisible (false);
    }
}

// ---------------------------------------------------------------------------
void Talk2MeEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Factory");
    const auto& fp = factoryPresets();
    const auto cur = proc.presets.getCurrentName();
    for (int i = 0; i < (int) fp.size(); ++i) m.addItem (1 + i, fp[(size_t) i].name, true, cur == fp[(size_t) i].name);
    const auto users = proc.presets.userPresets();
    if (! users.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("User");
        for (int i = 0; i < users.size(); ++i)
            m.addItem (1000 + i, users[i].getFileNameWithoutExtension(), true, cur == users[i].getFileNameWithoutExtension());
    }
    m.addSeparator();
    m.addItem (2000, "Save preset...");
    m.addItem (2001, "Show presets folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [this, users] (int r)
    {
        if (r >= 1 && r < 1000) proc.presets.loadFactory (r - 1);
        else if (r >= 1000 && r < 2000 && r - 1000 < users.size()) proc.presets.loadUser (users[r - 1000]);
        else if (r == 2000) savePreset();
        else if (r == 2001)
        {
            auto dir = PresetManager::userFolder();
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

void Talk2MeEditor::savePreset()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Settings and the phrase are saved together.", juce::MessageBoxIconType::NoIcon);
    saveWindow->addTextEditor ("name", proc.presets.getCurrentName(), "Name");
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && saveWindow != nullptr)
            if (! proc.presets.save (saveWindow->getTextEditorContents ("name"), proc.getText()))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Talk2Me", "Could not save preset.");
        saveWindow.reset();
    }), false);
}

void Talk2MeEditor::showSettingsMenu()
{
    juce::PopupMenu m, appearance, size;
    appearance.addItem (201, "Match system", true, themeMode == ThemeMode::System);
    appearance.addSeparator();
    appearance.addItem (202, "Oxygen (light)", true, themeMode == ThemeMode::Oxygen);
    appearance.addItem (203, "Rubber (dark)", true, themeMode == ThemeMode::Rubber);
    m.addSubMenu ("Appearance", appearance);
    for (int pct : { 75, 100, 125, 150 })
        size.addItem (100 + pct, juce::String (pct) + "%", true, std::abs (getWidth() - kBaseW * pct / 100) < 4);
    m.addSubMenu ("Size", size);
    m.addSeparator();
    m.addItem (2000, "Save preset...");
    m.addSeparator();
    m.addItem (5, "About Talk2Me");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton), [this] (int r)
    {
        if (r == 5) showAbout (true);
        else if (r == 2000) savePreset();
        else if (r == 201) setThemeMode (ThemeMode::System);
        else if (r == 202) setThemeMode (ThemeMode::Oxygen);
        else if (r == 203) setThemeMode (ThemeMode::Rubber);
        else if (r > 100 && r < 300)
        {
            const int pct = r - 100;
            if (auto prefs = openPrefs()) { prefs->setValue ("scale", pct); prefs->saveIfNeeded(); }
            setSize (kBaseW * pct / 100, kBaseH * pct / 100);
        }
    });
}

// ---------------------------------------------------------------------------
void Talk2MeEditor::updateEngineControls()
{
    const int eng = (int) proc.apvts.getRawParameterValue (ParamId::engine)->load();
    const int play = (int) proc.apvts.getRawParameterValue (ParamId::play)->load();
    if (eng == shownEngine && play == shownPlay) return;
    shownEngine = eng;
    shownPlay = play;
    const bool talk = eng == 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &voiceChoice, &formant, &breath, &buzz, &lofi }) c->setVisible (talk);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &chipChoice, &bend, &drive, &glitch, &lofi2, &chipPitch }) c->setVisible (! talk);
    loop.setEnabled (play == 0);
    shownBundle = nullptr;   // перерисовать «партитуру» (у Talk — дорожки формант)
    repaint();
}

void Talk2MeEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::window);
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));

    LnF::drawPlate (g, toolbarArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawVerticalLine (logo.getRight() + 8, (float) toolbarArea.getY() + 15, (float) toolbarArea.getBottom() - 15);

    g.setColour (Colours::surface);
    g.fillRoundedRectangle (panelArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (panelArea.toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.drawHorizontalLine (footArea.getY(), (float) footArea.getX(), (float) footArea.getRight());
    g.drawVerticalLine (dividerX, (float) leftHead.getY(), (float) footArea.getY() - 14.0f);

    // описание режима игры
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawFittedText (playDescription (shownPlay), leftHead.translated (0, 38).withHeight (32), juce::Justification::topLeft, 2, 1.0f);

    // заголовок движка
    const bool talk = shownEngine == 0;
    auto ta = rightHead;
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    g.drawText (talk ? juce::String ("Talk 96") + midDot() + "Formant voice" : juce::String ("Spell 80") + midDot() + "LPC chip",
                ta.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawFittedText (talk ? "A mid-90s formant text-to-speech voice. Notes set the pitch, Intonation adds the melody of speech."
                           : "The phrase is encoded into 25 ms LPC frames and played by an emulated Texas Instruments speech chip.",
                      ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
}

void Talk2MeEditor::resized()
{
    content.setBounds (0, 0, kBaseW, kBaseH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
    about.setBounds (0, 0, kBaseW, kBaseH);

    auto r = juce::Rectangle<int> (0, 0, kBaseW, kBaseH).reduced (14);
    toolbarArea = r.removeFromTop (52);
    {
        auto h = toolbarArea.reduced (10, 10);
        const auto logoSize = LnF::pixelTextSize ("Talk2Me", 1);
        logo.setBounds (h.removeFromLeft (juce::jmax (64, logoSize.x + 8)));
        h.removeFromLeft (18);
        prevPreset.setBounds (h.removeFromLeft (32));
        presetName.setBounds (h.removeFromLeft (150));
        nextPreset.setBounds (h.removeFromLeft (32));
        settingsButton.setBounds (h.removeFromRight (32));
        h.removeFromRight (10);
        outMeter.setBounds (h.removeFromRight (140));
        h.removeFromRight (14);
        transpose.setBounds (h.removeFromRight (112));
        h.removeFromRight (14);
        mini.setBounds (h.removeFromRight (96).withSizeKeepingCentre (96, 32));
        engineSwitch.setBounds (toolbarArea.withSizeKeepingCentre (200, 32));
    }
    r.removeFromTop (10);
    phrase.setBounds (r.removeFromTop (132));
    r.removeFromTop (10);
    scriptView.setBounds (r.removeFromTop (236));
    r.removeFromTop (10);
    panelArea = r;

    auto inner = panelArea.reduced (18, 14);
    footArea = inner.removeFromBottom (44);
    auto left = inner.removeFromLeft ((int) ((float) inner.getWidth() * 0.47f));
    dividerX = left.getRight() + 18;
    inner.removeFromLeft (36);
    auto right = inner;

    leftHead = left.removeFromTop (30);
    playSwitch.setBounds (leftHead.withWidth (juce::jmin (leftHead.getWidth(), 380)));
    left.removeFromTop (44);
    layoutRow (left, { &rate, &intonation, &glide, &vibrato, &release });

    rightHead = right.removeFromTop (44);
    right.removeFromTop (30);
    {
        auto row = right;
        auto pill = row.removeFromLeft (180);
        voiceChoice.setBounds (pill.withSizeKeepingCentre (176, 28));
        chipChoice.setBounds (pill.withSizeKeepingCentre (176, 28));
        row.removeFromLeft (8);
        layoutRow (row, { &formant, &breath, &buzz, &lofi });
        layoutRow (row, { &bend, &drive, &glitch, &lofi2 });
    }

    auto b = footArea.withTrimmedTop (10);
    poly.setBounds (b.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    b.removeFromLeft (4);
    loop.setBounds (b.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    b.removeFromLeft (4);
    chipPitch.setBounds (b.removeFromLeft (120).withSizeKeepingCentre (120, 28));
    status.setBounds (b.removeFromRight (220));
}

void Talk2MeEditor::timerCallback()
{
    // волна выхода → главный вид и мини-спектрограмма
    const int ready = proc.scopeFifo.getNumReady();
    if (ready > 0)
    {
        const auto rd = proc.scopeFifo.read (ready);
        if (! about.isVisible())
        {
            for (auto [start, size] : { std::pair<int, int> { rd.startIndex1, rd.blockSize1 }, std::pair<int, int> { rd.startIndex2, rd.blockSize2 } })
                if (size > 0)
                {
                    scriptView.push (proc.scopeBuf.data() + start, size);
                    mini.push (proc.scopeBuf.data() + start, size);
                }
        }
    }
    if (about.isVisible()) return;

    updateEngineControls();
    mini.setSampleRate (proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0);
    mini.tick();
    scriptView.tick();

    auto b = proc.getLatestBundle();
    if (b != shownBundle)
    {
        shownBundle = b;
        phrase.setBundle (b);
        scriptView.setBundle (b, shownEngine);
    }
    std::vector<float> times;
    for (auto& v : proc.engine.info)
        if (v.active.load (std::memory_order_relaxed)) times.push_back (v.t.load (std::memory_order_relaxed));
    phrase.setActive (times);
    scriptView.setPlayheads (times);

    outMeter.setLevel (proc.outPeak.load());
    presetName.setButtonText (proc.presets.getCurrentName());
    presetName.setDescription ("Preset " + proc.presets.getCurrentName());
    status.setText ("Voices " + juce::String (proc.engine.activeVoices.load()) + "/" + juce::String (Engine::kVoices)
                    + "   CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + "%", juce::dontSendNotification);
}
} // namespace t2m
