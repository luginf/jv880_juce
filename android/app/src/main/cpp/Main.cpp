// Android Standalone build of the emulator (Alan's request, 2026-09-09 - "comme on a fait pour
// le D110"), same shape as the D-110 Android port this was copied from
// (~/src/D110/d110-vst-emulator/android, see its own docs/android.md and
// .claude/dev-notes/android.md for the lessons this file leans on): one app, no plugin wrapper,
// CMake target `jiv881Android`, Gradle module `android/app`, app id `com.jiv881.android`.
//
// The real firmware/native core (VirtualJVProcessor, unchanged), the real photographed front
// panel (PanelSkin, kCompact variant - unlike D110Panel this one already self-scales to
// whatever width it's given, see PanelSkin::resized()/heightForWidth(), so no parent
// setTransform trick is needed here), the on-screen keyboard (VirtualKeyboard, which also
// already draws its own octave +/- buttons - no separate Options control needed for that), the
// patch browser (PatchBrowser, replacing the D110 port's SoundbankBrowser - JV-880 patches come
// from ROM/expansion boards, not imported SysEx banks, so there is no soundbank-import flow
// here) and the sequencer (JivSequencerPanel) - all reused unchanged from the desktop plugin.
// Deliberately NOT included: the tabbed extended editor (Common/Tone/Rhythm/Settings/Interface/
// Performance tabs) - same "as simple as possible" scope call the D110 port made, and nothing
// here has needed it. PluginEditor.cpp is still linked in (see app/CMakeLists.txt's own
// comment): VirtualJVProcessor::createEditor() references VirtualJVEditor's constructor, so its
// whole dependency graph has to be present at link time even though this app never calls it.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/PanelSkin.h"
#include "ui/PatchBrowser.h"
#include "ui/widgets/VirtualKeyboard.h"
#include "sequencer/JivSequencerPanel.h"

class MainComponent : public juce::Component, private juce::Timer {
public:
	// The app's own external files dir - the one location the native core can always read with
	// a plain filesystem path on Android (content:// SAF results can't be opened by a raw
	// FileInputStream - see chooseRomFolder()'s own comment). Same convention the D110 Android
	// port uses for its own romBringUpDir().
	static juce::File romBringUpDir() {
		return juce::File("/storage/emulated/0/Android/data/com.jiv881.android/files/roms");
	}

	MainComponent()
		: panelDisplay(processor, PanelSkin::Variant::kCompact),
		  keyboard(processor) {
		bringUpRoms();
		loadPersistedState();

		// No Settings tab exists in this app to flip the sequencer on - it's the whole reason
		// Alan asked for this port, so just turn it on unconditionally. Safe to call before
		// ROMs are loaded (loadSequencerState() just finds nothing to resolve yet); calling it
		// again once ROMs actually become available isn't needed - existing tracks keep
		// whatever they already had, and newly-assignable patches only matter once the user
		// visits Browse anyway.
		processor.setSequencerEnabled(true);

		addAndMakeVisible(panelDisplay);
		addAndMakeVisible(keyboard);
		createPatchDependentViews();

		menuButton.setButtonText(juce::String::fromUTF8("\xe2\x98\xb0")); // U+2630 "hamburger"
		menuButton.onClick = [this] { showMainMenu(); };
		addAndMakeVisible(menuButton);

		statusLabel.setJustificationType(juce::Justification::centredRight);
		statusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
		updateStatus();
		addAndMakeVisible(statusLabel);

		// AudioProcessorPlayer is what JUCE's own desktop Standalone wrapper
		// (StandaloneFilterWindow) uses internally to bridge a real AudioProcessor to a live
		// device - same idea here, with no window chrome/audio-settings dialog to go with it.
		// Its audioDeviceAboutToStart() is what actually calls processor.prepareToPlay().
		// Registered as both the audio callback AND every MIDI input device's callback (it
		// implements MidiInputCallback too) - the same object routes MIDI input straight into
		// processBlock()'s buffer, no bespoke bridge of our own needed.
		deviceManager.initialiseWithDefaultDevices(0, 2);
		player.setProcessor(&processor);
		deviceManager.addAudioCallback(&player);
		scanForMidiInputs();
		// android.media.midi has no "device changed" callback exposed through JUCE, only a
		// snapshot list (JUCE MidiInput::getAvailableDevices()) - polled here rather than once,
		// so a USB keyboard plugged in after launch is picked up too. Same technique the D110
		// Android port uses for its own scanForMidiInputs().
		startTimer(2000);

		setSize(900, 500);
	}

	~MainComponent() override {
		saveState();
		stopTimer();
		deviceManager.removeAudioCallback(&player);
		player.setProcessor(nullptr);
		for (const auto &id : knownMidiInputs) {
			deviceManager.removeMidiInputDeviceCallback(id, &player);
			deviceManager.setMidiInputDeviceEnabled(id, false);
		}
	}

	// See D110AndroidApp::suspended()'s own comment for why this has to be reachable from
	// outside a clean destructor chain too - Android can kill this whole process without
	// warning once backgrounded.
	void saveState() {
		juce::MemoryBlock data;
		processor.getStateInformation(data);
		stateFile().getParentDirectory().createDirectory();
		stateFile().replaceWithData(data.getData(), data.getSize());
		if (processor.getSequencerEnabled())
			processor.saveSequencerState();
	}

	void paint(juce::Graphics &g) override { g.fillAll(juce::Colour(0xff1e1e22)); }

	void resized() override {
		// Same fixed-guess safe-area handling the D110 Android port settled on (see its own
		// resized() comment for the full story of why a LIVE getDisplayForRect() lookup was
		// abandoned): a status-bar-sized inset on top always, and a nav-bar-sized inset on
		// whichever edge closes the current orientation (right in landscape, bottom in
		// portrait), with four independent manual overrides (Options menu) as an escape hatch
		// for devices that guess wrong (e.g. a tablet keeping its nav bar bottom-anchored even
		// in landscape).
		auto area = getLocalBounds();
		const bool isLandscape = getWidth() > getHeight();
		constexpr int kStatusBarInset = 32;
		constexpr int kNavBarInset = 64;
		juce::BorderSize<int> insets;
		if (navTop || navBottom || navLeft || navRight) {
			insets = {navTop ? kStatusBarInset : 0, navLeft ? kNavBarInset : 0,
			          navBottom ? kNavBarInset : 0, navRight ? kNavBarInset : 0};
		} else {
			insets = isLandscape ? juce::BorderSize<int>(kStatusBarInset, 0, 0, kNavBarInset)
			                     : juce::BorderSize<int>(kStatusBarInset, 0, kNavBarInset, 0);
		}
		area = insets.subtractedFrom(area);

		// The sequencer has its own transport row with a bar-menu button that already carries
		// the app's own hamburger (see onBarMenuButtonExtra below), so hide this row while it's
		// showing and give it the full height instead - same reasoning as the D110 port's own
		// grid-sequencer treatment.
		const bool inSequencer = currentView == View::Sequencer;
		menuButton.setVisible(!inSequencer);
		statusLabel.setVisible(!inSequencer);
		if (!inSequencer) {
			auto row = area.removeFromTop(44);
			menuButton.setBounds(row.removeFromRight(48).reduced(4));
			statusLabel.setBounds(row.reduced(8, 4));
		}

		panelDisplay.setVisible(currentView == View::Keyboard);
		keyboard.setVisible(currentView == View::Keyboard);
		patchBrowser->setVisible(currentView == View::Browse);
		sequencerPanel->setVisible(currentView == View::Sequencer);

		if (currentView == View::Browse) {
			patchBrowser->setBounds(area);
		} else if (currentView == View::Sequencer) {
			sequencerPanel->setBounds(area);
		} else {
			const int panelH = juce::roundToInt(panelDisplay.heightForWidth((float)area.getWidth()));
			panelDisplay.setBounds(area.removeFromTop(panelH));
			// Giving it the WHOLE remaining area (as landscape's wide-but-short leftover space
			// warrants) made portrait's tall leftover space stretch the keys absurdly high
			// (Alan's report) - VirtualKeyboard::kRefH (120, the desktop reference height) is
			// too short for comfortable touch targets, so this caps it at a modest multiple of
			// that instead of either extreme.
			const int kbH = juce::jmin(area.getHeight(), juce::roundToInt(VirtualKeyboard::kRefH * 2.2f));
			keyboard.setBounds(area.removeFromTop(kbH));
		}
	}

private:
	enum class View { Keyboard, Browse, Sequencer };

	// (Re)creates the two views whose content is only meaningful once ROMs are loaded -
	// PatchBrowser snapshots processor.patchInfos[] at construction with no live-refresh hook
	// of its own, and the sequencer's track patches are resolved against that same table (see
	// loadSequencerState()'s own comment). Called once from the constructor (before ROMs are
	// necessarily present - a fresh install shows empty lists until "Choose ROM files..."
	// succeeds) and again from retryRoms() the moment loading actually succeeds.
	void createPatchDependentViews() {
		if (patchBrowser) removeChildComponent(patchBrowser.get());
		patchBrowser = std::make_unique<PatchBrowser>(processor);
		addChildComponent(*patchBrowser);

		if (sequencerPanel) removeChildComponent(sequencerPanel.get());
		sequencerPanel = std::make_unique<JivSequencerPanel>(processor);
		addChildComponent(*sequencerPanel);
		// See buildAppMenu()'s own comment: the sequencer hides the app's own hamburger row to
		// get its full height, so its bar-menu button becomes the only way back to it.
		sequencerPanel->onBarMenuButtonExtra = [this](juce::PopupMenu &m) { buildAppMenu(m); };
	}

	void setView(View v) {
		if (v == currentView) return;
		currentView = v;
		resized();
	}

	static juce::File stateFile() {
		return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
			.getChildFile("jiv881-state.bin");
	}

	void loadPersistedState() {
		auto f = stateFile();
		if (!f.existsAsFile()) return;
		juce::MemoryBlock data;
		if (f.loadFileAsData(data) && data.getSize() > 0)
			processor.setStateInformation(data.getData(), (int)data.getSize());
	}

	// Points the native core at romBringUpDir() if it already has files in it (a previous
	// "Choose ROM files..." run, or the user `adb push`-ed them there directly - see
	// docs/android.md) and retries loading. Harmless no-op once processor.loaded is already
	// true (retryLoadRoms() guards on that itself), and harmless if the folder doesn't exist
	// yet either.
	void bringUpRoms() {
		const auto dest = romBringUpDir();
		if (dest.isDirectory()) {
			processor.setRomsFolderOverride(dest);
			processor.retryLoadRoms();
		}
	}

	void updateStatus(const juce::String &override_ = {}) {
		if (!override_.isEmpty()) {
			statusLabel.setText(override_, juce::dontSendNotification);
			return;
		}
		statusLabel.setText(processor.loaded ? "Ready" : "ROMs not found - use menu to choose files",
		                     juce::dontSendNotification);
	}

	// Android has no working way to list a picked SAF folder's contents (see the D110 Android
	// port's own dev-notes for why AndroidDocumentIterator's tree-walking is dead code in this
	// JUCE version) - so this asks for the ROM files themselves (multi-select) and copies them
	// into a real, plain-filesystem staging folder instead. loadRom() matches files by their
	// EXACT expected filename (see rom.cpp's own romInfos[] table), so - unlike the D110 port,
	// which sniffs file content - the copy has to preserve each file's real display name.
	void chooseRomFolder() {
		fileChooser = std::make_unique<juce::FileChooser>(
			"Choose your JV-880 ROM files (select all of them at once)", juce::File());
		fileChooser->launchAsync(
			juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
				| juce::FileBrowserComponent::canSelectMultipleItems,
			[this](const juce::FileChooser &fc) { copyRomFilesAndReload(fc.getURLResults()); });
	}

	void copyRomFilesAndReload(const juce::Array<juce::URL> &urls) {
		const auto dest = romBringUpDir();
		dest.createDirectory();

		int copied = 0;
		for (const auto &url : urls) {
			auto doc = juce::AndroidDocument::fromDocument(url);
			auto in = doc.hasValue() ? doc.createInputStream() : nullptr;
			if (in == nullptr) continue;

			// url.getFileName() is unreliable for a content:// SAF result (just whatever
			// follows the URI STRING's last literal '/', not the real display name) -
			// AndroidDocumentInfo::getName() is backed by SAF's real DISPLAY_NAME column
			// instead. Same lesson the D110 Android port already learned once (see its own
			// dev-notes/android.md).
			juce::String name = doc.getInfo().getName();
			if (name.isEmpty()) name = url.getFileName();

			auto outFile = dest.getChildFile(name);
			outFile.deleteFile();
			auto out = outFile.createOutputStream();
			if (out == nullptr || out->writeFromInputStream(*in, -1) <= 0) {
				outFile.deleteFile();
				continue;
			}
			out.reset();
			++copied;
		}

		if (copied == 0) {
			updateStatus("None of those files could be read");
			return;
		}

		// Matches the desktop Settings tab's own "Reload ROMs" button, which likewise disables
		// itself once processor.loaded is true (see PluginProcessor.h's own comment on
		// attemptLoadRoms() - switching ROM folders on an already-loaded engine isn't
		// supported, only a fresh boot re-scans the folder). Files just copied here ARE
		// already sitting in romBringUpDir() correctly named - see chooseRomFolder()'s own
		// comment - they just won't actually load until the app restarts, so say that
		// explicitly instead of silently doing nothing (Alan's report, 2026-09-09: picking
		// expansion-board files after the app was already running from a prior ROM boot copied
		// them fine but Browse still only showed the original factory banks).
		if (processor.loaded) {
			updateStatus(juce::String(copied) + " file(s) copied - close and reopen the app to load them");
			return;
		}

		retryRoms();
	}

	void retryRoms() {
		const bool wasLoaded = processor.loaded;
		bringUpRoms();
		if (!wasLoaded && processor.loaded)
			createPatchDependentViews();
		updateStatus();
		resized();
	}

	// Shared between the app's own hamburger button and the sequencer's own bar-navigation
	// menu button (see createPatchDependentViews()'s own comment) - every item is a
	// self-contained action callback so it can be dropped into either menu unchanged.
	void buildAppMenu(juce::PopupMenu &m) {
		// Reachable unconditionally, not gated behind processor.loaded - this is exactly the
		// thing to reach for when ROMs are missing and the panel/keyboard are sitting empty.
		m.addItem("Choose ROM files...", [this] { chooseRomFolder(); });
		if (!processor.loaded)
			m.addItem("Retry loading ROMs", [this] { retryRoms(); });
		m.addSeparator();
		m.addItem(currentView == View::Browse ? "Front Panel" : "Browse patches...",
		          [this] { setView(currentView == View::Browse ? View::Keyboard : View::Browse); });
		m.addItem(currentView == View::Sequencer ? "Front Panel" : "Sequencer",
		          [this] { setView(currentView == View::Sequencer ? View::Keyboard : View::Sequencer); });
		// VirtualKeyboard::showContextMenu() is the exact same channel/remap/PC-keyboard menu
		// the desktop keyboard's right-click shows - reached here directly instead of
		// reimplementing it, since there's no right mouse button on a touchscreen.
		m.addItem("Keyboard channel...", [this] { keyboard.showContextMenu(); });
		m.addSeparator();

		// Manual escape hatch for resized()'s own nav-bar-avoidance guess - see its own
		// comment. Flat items rather than a further-nested submenu, same reasoning as the D110
		// Android port's own Options menu: one fewer nesting level for a popup that's already
		// two deep when reached from the sequencer's own bar menu.
		juce::PopupMenu options;
		options.addItem("Auto margins (Recommended)", true, !navTop && !navBottom && !navLeft && !navRight,
		                 [this] { navTop = navBottom = navLeft = navRight = false; resized(); });
		options.addItem("Margin: top", true, navTop, [this] { navTop = !navTop; resized(); });
		options.addItem("Margin: bottom", true, navBottom, [this] { navBottom = !navBottom; resized(); });
		options.addItem("Margin: left", true, navLeft, [this] { navLeft = !navLeft; resized(); });
		options.addItem("Margin: right", true, navRight, [this] { navRight = !navRight; resized(); });
		m.addSubMenu("Options", options);
	}

	void showMainMenu() {
		juce::PopupMenu m;
		buildAppMenu(m);
		m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton));
	}

	// Every discovered device is enabled and its callback pointed straight at `player`
	// (AudioProcessorPlayer already implements MidiInputCallback, forwarding incoming messages
	// into processor.processBlock()'s own MidiBuffer) - class-compliant USB MIDI needs no
	// pairing/permission dialog the way Bluetooth would.
	void scanForMidiInputs() {
		for (const auto &device : juce::MidiInput::getAvailableDevices()) {
			if (knownMidiInputs.contains(device.identifier)) continue;
			knownMidiInputs.add(device.identifier);
			deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
			deviceManager.addMidiInputDeviceCallback(device.identifier, &player);
		}
	}

	void timerCallback() override { scanForMidiInputs(); }

	// Declared first so its default constructor runs before `processor` below is constructed
	// (member init order follows declaration order) - makes VirtualJVProcessor::wrapperType
	// read as wrapperType_Standalone, exactly like JUCE's own Standalone plugin-client wrapper
	// does right before constructing its filter (setTypeOfNextNewPlugin() feeds a thread-local
	// value the AudioProcessor base constructor reads once, during its own init list). Unlike
	// the D110 port (whose sequencer processing has no such gate), this project's
	// processBlock() explicitly requires wrapperType_Standalone before the sequencer/metronome
	// logic runs at all (see PluginProcessor.cpp's own
	// `wrapperType == wrapperType_Standalone && sequencerEnabled` checks) - without this,
	// PLAY/REC on Android would silently do nothing.
	struct WrapperTypeSetter {
		WrapperTypeSetter() {
			juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
		}
	};
	WrapperTypeSetter wrapperTypeSetter;

	VirtualJVProcessor processor;

	View currentView = View::Keyboard;
	bool navTop = false, navBottom = false, navLeft = false, navRight = false;

	PanelSkin panelDisplay;
	VirtualKeyboard keyboard;
	std::unique_ptr<PatchBrowser> patchBrowser;
	std::unique_ptr<JivSequencerPanel> sequencerPanel;

	juce::TextButton menuButton;
	juce::Label statusLabel;

	std::unique_ptr<juce::FileChooser> fileChooser;

	juce::AudioDeviceManager deviceManager;
	juce::AudioProcessorPlayer player;
	juce::StringArray knownMidiInputs;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

class MainWindow : public juce::DocumentWindow {
public:
	explicit MainWindow(const juce::String &name)
		: DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::allButtons) {
		setUsingNativeTitleBar(true);
		auto *content = new MainComponent();
		mainComponent = content;
		setContentOwned(content, true);
		setFullScreen(true);
		setVisible(true);
	}

	void closeButtonPressed() override {
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}

	void saveState() { mainComponent->saveState(); }

private:
	MainComponent *mainComponent = nullptr;
};

class JiV881AndroidApp : public juce::JUCEApplication {
public:
	const juce::String getApplicationName() override { return "jiv881"; }
	const juce::String getApplicationVersion() override { return "0.1"; }

	void initialise(const juce::String &) override {
		mainWindow = std::make_unique<MainWindow>(getApplicationName());
	}

	void shutdown() override {
		if (mainWindow) mainWindow->saveState();
		mainWindow = nullptr;
	}

	// The save point that actually matters on Android: called when the OS backgrounds this
	// app, and the process can be killed with no further warning at any point afterwards -
	// well before shutdown() above would ever run. A plain in-app quit (which does reach
	// shutdown()) is the rare case; going to the home screen or switching apps is the common
	// one, and only this callback reliably fires for that.
	void suspended() override {
		if (mainWindow) mainWindow->saveState();
	}

private:
	std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(JiV881AndroidApp)
