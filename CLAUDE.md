# CLAUDE.md

Notes de contexte pour Claude Code sur ce projet (émulateur JUCE du Roland JV-880, basé sur Nuked-SC55).

## Mode Performance

### Le mode Performance sur le vrai JV-880

Vérifié dans le manuel d'origine (pas seulement de mémoire) :

- Une Performance combine **8 Parties** : 7 Parties "Patch" + 1 Partie Rhythm Set fixe (Partie 8).
  Ce n'est PAS 4 parties.
- Mémoire : 16 Performances en interne (réinscriptibles), + 16 en Preset A et 16 en Preset B
  (lecture seule, copiables vers l'interne), + 16 sur Data Card en option.
- Par Partie : Patch (ou Rhythm Set pour la 8e) assigné, canal MIDI de réception, niveau, pan,
  transposition (coarse/fine), sortie Main/Sub, interrupteurs chorus/reverb, et quelques switches
  de réception (Program Change, Hold-1, Volume).
- Chorus et reverb sont **communs à toute la Performance** (un seul bus d'effets partagé), pas
  réglables par partie.
- Le bouton **PATCH/PERFORM** du panneau bascule entre mode Patch (un seul son) et mode
  Performance (multitimbral).

### Pourquoi ce n'est pas implémenté à l'identique ici

Le moteur audio de ce projet (`Source/emulator/mcu.cpp` et alentours) est une émulation
cycle-accurate du vrai CPU du JV-880 qui exécute le **firmware d'origine** (héritage
Nuked-SC55). Le vrai mode Performance existe donc bel et bien dans la ROM émulée -
`MCU_BUTTON_PATCH_PERFORM` est défini dans `Source/emulator/mcu.h` - mais cet enum n'est **jamais
utilisé nulle part** dans le code. L'UI JUCE actuelle ne simule aucun appui de bouton du panneau
physique : elle contourne entièrement la logique du firmware et écrit directement dans la NVRAM
émulée à deux offsets connus et vérifiés (`0x0d70` pour la zone Patch Temp, `0x67f0` pour Rhythm
Temp - voir `VirtualJVProcessor::setCurrentProgram()` dans `Source/PluginProcessor.cpp`).

La zone NVRAM "Performance Temp" réelle - son offset, sa taille, le flag de mode qui ferait
tourner un vrai Performance 8 parties dans le moteur émulé - n'a jamais été rétro-ingénierée dans
ce projet. Deviner ces valeurs à l'aveugle serait risqué (corruption NVRAM ou simplement aucun
effet), et le travail de recherche nécessaire (tracer les accès mémoire du CPU émulé pendant une
simulation d'appui sur PATCH/PERFORM, ou trouver une documentation/désassemblage existant) est à
durée indéterminée.

### Ce qui a été construit à la place (2026-09-07)

Un **clone pragmatique côté JUCE**, pas une activation du vrai mode Performance du firmware :

- Nouvel onglet **Performance** juste après Browse (`Source/ui/PerformanceTab.h/.cpp`).
- Clic droit sur un patch/rythme dans Browse → "Send to Performance Slot 1-4"
  (`PatchesListModel::listBoxItemClicked` dans `Source/ui/PatchBrowser.h`).
- **4 slots** (pas 8), chacun avec : canal MIDI de réception (All ou 1-16), niveau, pan, on/off,
  bouton Clear.
- Sauvegarde nommée dans une banque dédiée sur disque (`~/.config/JV880/Performances/*.jvpf`),
  listée et rechargeable dans l'onglet - même esprit que la banque "User" des patches
  (`saveCurrentPatchAs`/`refreshUserPatches`/`.jvp` déjà existants).
- Moteur audio : **4 instances `MCU` parallèles indépendantes** (une par slot), chacune chargée
  via le même mécanisme direct-NVRAM déjà utilisé pour le Patch mode, mixées en logiciel dans
  `VirtualJVProcessor::processBlock()`. Allouées paresseusement (~20 Mo/instance) seulement à la
  première activation du mode Performance.
- État "en cours d'édition" (4 slots + mode activé/désactivé) **persisté entre redémarrages**
  (2026-09-07, suite au retour d'Alan) mais **pas dans le projet DAW** - `DataToSave`
  (`PluginProcessor.h`) est un blob à taille fixe recopié tel quel par `memcpy`, y ajouter des
  champs casserait le chargement d'anciens projets. À la place, un fichier séparé
  `~/.config/JV880/performance_session.dat` (voir `performanceSessionFile()`,
  `savePerformanceSessionState()`/`loadPerformanceSessionState()` dans `PluginProcessor.cpp`) est
  réécrit après chaque mutation (envoi vers un slot, clear, changement de canal/niveau/pan/on-off,
  activation du mode, chargement d'une Performance sauvegardée) et relu au démarrage - même
  principe que `keyboardSettingsFile()`. Un "Save As..." reste nécessaire pour donner un nom et
  ranger une Performance dans la banque partagée listée dans l'onglet ; ce fichier de session est
  distinct (hors de `performancesDir()`, jamais listé dans la banque) et ne fait que restaurer
  "l'état de travail en cours".

Conséquences assumées de cette approche :
- Pas de bus chorus/reverb partagé entre slots (chaque slot garde les effets propres à son
  Patch/Rhythm Set).
- Pas de structure 7+1 fidèle (4 slots génériques, chacun patch OU rythme).
- Coût CPU jusqu'à ~4x quand le mode est actif (4 moteurs cycle-accurate qui tournent en série
  sous un seul verrou), accepté pour cette version - paralléliser les 4 moteurs sur un pool de
  threads est possible plus tard si besoin. En attendant, un buffer audio plus grand est le
  levier immédiat contre les craquements/xruns en mode Performance à 4 slots - voir la section
  Audio/MIDI Settings ci-dessous. Dans un DAW (VST3/AU/LV2), c'est le buffer size de l'hôte qui
  s'applique, pas ce réglage.

## Réglages Audio/MIDI (2026-09-07)

Alan n'aime pas la fenêtre séparée du wrapper Standalone de JUCE ("Settings..." en haut de la
fenêtre app, qui ouvre un `DialogWindow` flottant). Les réglages audio/MIDI (périphérique
entrée/sortie, MIDI actifs, et **sample rate/buffer size** dès qu'un vrai périphérique est
sélectionné) sont maintenant **intégrés directement dans l'onglet Settings**, sous les réglages
existants (Reverb/Chorus/Master Tune/Master Volume) - voir `SettingsTab.h`/`.cpp`.

Ça reste un `juce::AudioDeviceSelectorComponent` standard (mêmes contrôles que l'ancien dialogue),
juste embarqué dans notre propre UI au lieu d'un popup JUCE séparé. Point technique à retenir :
- Uniquement pertinent en build **Standalone** (une IO buffer size n'a de sens que là - en
  plugin dans un DAW, c'est l'hôte qui possède le périphérique audio). Détection à l'exécution via
  `processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone` et
  `juce::StandalonePluginHolder::getInstance()` (retourne `nullptr` si on tourne en VST3/AU/LV2).
- Ce projet compile AU/LV2/Standalone/VST3 depuis une seule "Shared Code" (voir
  `Builds/LinuxMakefile`) - `JucePlugin_Build_Standalone` vaut donc 1 pour toute cette
  compilation partagée, peu importe le wrapper final. `StandalonePluginHolder` est entièrement
  header-only (pas de symbole à lier séparément), donc inclure son header sous
  `#if JucePlugin_Build_Standalone` compile et linke sans problème dans tous les formats - seul le
  test `getInstance()` à l'exécution distingue vraiment "on tourne en standalone" de "on tourne en
  plugin". Confirmé : build réussi pour VST3/LV2/Standalone après cet ajout.
- Si `getInstance()` renvoie `nullptr` (build plugin, ou Standalone pas encore initialisé), un
  message texte remplace le sélecteur ("Audio device and buffer size are controlled by your
  DAW/host...").

### Charge DSP (2026-09-07)

`VirtualJVProcessor::dspLoadMeasurer` (`juce::AudioProcessLoadMeasurer`, fourni par JUCE - voir
`juce_audio_basics/buffers/juce_AudioProcessLoadMeasurer.h`) mesure la proportion du budget
temps-réel de chaque bloc passée dans `processBlock()`, via un `ScopedTimer` qui englobe tout le
bloc (gestion MIDI + rendu, Patch mode ou Performance mode indifféremment). Lecture atomique/
lock-free, affichée dans l'onglet Settings (label "DSP Load: NN.N %", `SettingsTab::dspLoadLabel`,
rafraîchi 4x/seconde via un `juce::Timer`). Vérifié empiriquement dans Xvfb : ~17% en Patch mode
au repos contre ~85% avec 4 slots actifs en Performance - confirme concrètement le surcoût ~4x
déjà documenté plus haut, et donne à Alan un repère direct pour choisir sa taille de buffer.

Détails complets de la conception (structures de données, format `.jvpf` octet par octet, bugs
trouvés/corrigés pendant les tests) : voir l'historique de conversation Claude Code du
2026-09-07, ou directement le code (`PluginProcessor.h`/`.cpp`, `PerformanceTab.h`/`.cpp`,
`PatchBrowser.h`).

### Piste future : rétro-ingénierie du vrai mode Performance

Si le temps le permet un jour, une piste plus fidèle au hardware serait de rétro-ingénier la
vraie zone NVRAM "Performance Temp" et le vrai flag de mode du firmware émulé, pour piloter le
**vrai** mode Performance 8 parties (moteur unique, effets partagés, fidèle à 100 % au hardware)
au lieu du clone à 4 moteurs parallèles décrit ci-dessus. Pistes de départ possibles :
- Tracer les accès mémoire du CPU émulé (`Source/emulator/mcu_opcodes.cpp`) pendant une
  simulation d'appui sur le bouton `MCU_BUTTON_PATCH_PERFORM` (actuellement jamais déclenché nulle
  part dans le code).
- Chercher si un désassemblage ou une documentation d'adressage SysEx du JV-880 (zone Performance
  Temporaire) existe déjà quelque part (communauté MAME, reverse-engineering du JV-880/JV-880
  MIDI implementation chart officiel Roland).

Ce n'est pas un chantier ouvert actuellement - à reprendre seulement si Alan le demande
explicitement.
