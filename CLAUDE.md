# CLAUDE.md

Notes de contexte pour Claude Code sur ce projet (émulateur JUCE du Roland JV-880, basé sur Nuked-SC55).

## Mode Performance

> **Superseded (2026-09-08)** : les deux sections ci-dessous ("Pourquoi ce n'est pas implémenté
> à l'identique ici" et "Ce qui a été construit à la place") décrivent le clone à 4 moteurs
> parallèles qui a été le premier jet de cette fonctionnalité. Il a depuis été **entièrement
> remplacé** par le vrai mode Performance natif du firmware (1 seul moteur) - voir "Mode
> Performance v2" plus bas pour l'implémentation actuelle. Gardé tel quel pour l'historique/le
> contexte de la décision initiale.

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
alors en vigueur, et donne à Alan un repère direct pour choisir sa taille de buffer.

**Périmé depuis le passage au mode Performance v2 (2026-09-08, voir plus bas)** : Performance
mode tourne maintenant sur le même unique moteur que Patch mode, donc ce ~85 % n'a plus lieu
d'être - le DSP Load attendu en Performance est désormais du même ordre que Patch mode (~17 %),
quel que soit le nombre de Parts actives. Pas re-mesuré dans Xvfb (pas de device audio
disponible dans ce sandbox), mais attendu directement de l'architecture (un seul appel
`updateSC55WithSampleRate()` par bloc, comme en mode Patch).

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

#### Outils construits pour cette RE (2026-09-07)

Deux outils dormants (aucun coût quand désactivés) ajoutés pour permettre cette investigation
sans deviner :

- **Traçage NVRAM** (`Source/emulator/mcu.cpp`, `MCU_NvramTraceEnabled()` + le point d'écriture
  NVRAM dans `MCU::MCU_Write`, page 12) : avec `JV880_TRACE_NVRAM=1` dans l'environnement, chaque
  octet de `nvram[]` qui change de valeur est loggé sur stderr (`[nvram] xxxx: aa -> bb`).
  Vérifié une seule fois au premier appel (`static const bool`), donc sans coût quand la variable
  n'est pas définie.
- **Self-test headless** (`Source/PluginProcessor.cpp`, juste après `mcu->startSC55(...)` dans le
  constructeur de `VirtualJVProcessor`) : avec `JV880_SELFTEST_BUTTON=<séquence>` dans
  l'environnement, fait tourner l'émulateur (via `mcu->updateSC55WithSampleRate()`, en boucle,
  sans passer par un vrai périphérique audio ni par une fenêtre) pendant `JV880_SELFTEST_PRE_MS`
  ms (défaut 3000, laisse le firmware terminer son boot), puis exécute la séquence
  (identifiants `MCU_BUTTON_*` séparés par des virgules, ex. `10,11` = PATCH_PERFORM puis EDIT ;
  un token `e0`/`e1` déclenche une impulsion `MCU_EncoderTrigger` du dial data au lieu d'un
  bouton), chaque étape tenue `JV880_SELFTEST_HOLD_MS` ms (défaut 150) puis suivie d'un settle de
  `JV880_SELFTEST_POST_MS` ms (défaut 3000), écrit un snapshot NVRAM avant/après dans
  `/tmp/jv880_nvram_{before,after}.bin`, puis `exit(0)` - donc utilisable sans Xvfb (le
  constructeur du processor tourne avant toute création de fenêtre). Exemple :
  ```
  JV880_TRACE_NVRAM=1 JV880_SELFTEST_BUTTON=10,11 JV880_SELFTEST_PRE_MS=2000 \
  JV880_SELFTEST_HOLD_MS=150 JV880_SELFTEST_POST_MS=1500 \
  ./Builds/LinuxMakefile/build/jv880
  ```

**Résultat obtenu jusqu'ici** : appuyer sur `MCU_BUTTON_PATCH_PERFORM` (10) fait basculer
`nvram[0x11]` de `01` à `00` - exactement l'octet que ce projet écrit déjà "à la main"
(`mcu->nvram[0x11] = status.isDrums ? 0 : 1` dans `PluginProcessor.cpp`) pour choisir entre Patch
Temp et Rhythm Temp. C'est donc bien un flag de mode réel du firmware, pas une simple coïncidence
d'offset. En revanche, enchaîner EDIT (11), plusieurs CURSOR_R (1) et deux impulsions d'encodeur
(`e1`) après le PATCH/PERFORM n'a produit **aucune autre écriture NVRAM** dans ce test rapide - la
vraie zone "Performance Temp" à 8 parties n'a pas encore été localisée. Pistes à essayer pour la
suite : d'autres enchaînements de boutons (peut-être qu'il faut rester plus longtemps sur EDIT
avant que l'encodeur soit pris en compte, ou qu'un champ précis doive être sélectionné avant que
DATA ne fasse quelque chose), ou repartir sur la piste du traçage des accès mémoire du CPU émulé
plutôt que seulement les écritures NVRAM (la position du curseur ou les data de travail
pourraient vivre en RAM système `sram`/`ram`, jamais persistées, avant qu'une vraie valeur ne soit
validée).

### Onglet Interface (2026-09-07)

Nouvel onglet **Interface**, en dernière position dans les deux configurations de la
`TabbedComponent` (`Source/PluginEditor.cpp`), implémenté dans `Source/ui/InterfaceTab.h/.cpp`.
Contrairement à tous les autres onglets (qui éditent les données du patch/rythme directement en
mémoire, en court-circuitant le firmware - voir plus haut), celui-ci pilote le **vrai** chemin
d'entrée du firmware émulé :

- Les 14 boutons du panneau JV-880 (`MCU_BUTTON_CURSOR_L/R`, `TONE_SELECT`, `MUTE`, `DATA`,
  `MONITOR`, `COMPARE`, `ENTER`, `UTILITY`, `PREVIEW`, `PATCH_PERFORM`, `EDIT`, `SYSTEM`,
  `RHYTHM` - voir l'enum dans `Source/emulator/mcu.h`), chacun un bouton "momentané"
  (`InterfaceTab::PanelButton`, sous-classe de `juce::TextButton` qui appuie/relâche
  `processor.mcu->lcd.LCD_SendButton(id, état)` sur `mouseDown`/`mouseUp` - cette fonction
  existait déjà dans `lcd.cpp` mais n'était appelée nulle part avant cet ajout).
- Le dial data entry du panneau (rotatif sur le vrai JV-880, pas un slider) : deux boutons
  "Data -"/"Data +" (`InterfaceTab::DialButton`) qui envoient chacun une impulsion
  `processor.mcu->MCU_EncoderTrigger(dir)` par clic - autre fonction déjà présente
  (`Source/emulator/mcu.cpp`) mais jamais appelée avant cet ajout.
- L'écran LCD déjà affiché en permanence au-dessus des onglets (`LCDisplay`, toujours visible
  quel que soit l'onglet actif) réagit donc en direct aux appuis simulés depuis cet onglet.

Aucune synchronisation ajoutée pour ces écritures (`mcu_button_pressed`, un simple `uint32_t`) -
cohérent avec le reste du projet, qui touche déjà `mcu`/`nvram` depuis le thread UI sans lock nulle
part.

Sert un double usage : (1) outil d'investigation pour la RE du mode Performance natif ci-dessus,
et (2) à terme, chemin d'accès générique à tout écran firmware qui n'a pas (ou n'aura jamais)
d'équivalent en écriture NVRAM directe.

## Mode Performance v2 : le vrai mode natif du firmware, 1 seul moteur (2026-09-08)

**Le clone à 4 moteurs décrit ci-dessus a été entièrement remplacé.** Alan a signalé que 3 patches
actifs étaient déjà lourds, et qu'un 4e faisait grimper le DSP à ~100 %. La piste de RE marquée
"à reprendre seulement si Alan le demande" a été reprise, elle a abouti, et le mode Performance
tourne maintenant sur le **même moteur MCU unique** que le mode Patch - même coût CPU que le mode
Patch simple, quel que soit le nombre de parties actives (vérifié : plus de code de mixage à 4
moteurs du tout dans `processBlock()`).

### Comment ça a été trouvé

Piste donnée par Alan : `~/src/D110/edisyn/edisyn/synth/rolandjv880/` contient le support JV-880
complet du logiciel Edisyn (Sean Luke, licence Apache 2.0) - un éditeur de patches universel déjà
entièrement rétro-ingénié contre le vrai hardware. `RolandJV880Multi.java` (l'éditeur "Multi" =
Performance) et sa classe de reconnaissance `RolandJV880MultiRec.java` documentent la carte
d'adresses SysEx complète du mode Performance, directement transcrite depuis le MIDI
Implementation officiel Roland - jamais besoin d'avoir deviné un offset NVRAM à l'aveugle.

Le mécanisme clé (déjà présent partiellement dans ce projet via
`VirtualJVProcessor::sendSysexParamChange()`, utilisé par SettingsTab pour Master Tune/Reverb/
Chorus) : une commande SysEx Roland **DT1** (`F0 41 10 46 12 <adresse 4x7bit> <données...>
<checksum> F7`) envoyée dans le MIDI IN émulé (`mcu->postMidiSC55()`) est interprétée par le
**vrai firmware lui-même**, qui écrit la donnée où il faut en RAM - inutile de connaître l'offset
RAM réel, le firmware s'en charge, exactement comme le ferait un vrai JV-880 recevant un dump
d'un logiciel comme Edisyn.

### Preuve empirique (avant d'écrire le code final)

Une session de validation headless (sans device audio ni fenêtre - voir "Outils" ci-dessous) a
confirmé, dans l'ordre :
1. Écrire l'octet Système `00 00 00 00 = 0x00` bascule `nvram[0x11]` de `01` à `00` - **exactement**
   ce que fait l'appui simulé sur PATCH/PERFORM (bouton physique), confirmant qu'il s'agit bien du
   même flag de mode.
2. Écrire le bloc "Performance Common" (31 octets, nom + effets + voice reserve) et un bloc
   "Part" (35 octets, patch/canal/niveau/pan...) à l'adresse `00 00 10 00`/`00 00 1n 00`
   atterrissent en RAM système (`sram`, PAS `nvram` - d'où l'échec des premiers essais qui ne
   traçaient que la NVRAM battery-backed).
3. **Capture d'écran du LCD réel** (rendu par le firmware, dumpé en PNG directement depuis le
   self-test, sans fenêtre) : après ces écritures, l'écran affiche bien "Perf" avec les 8 parties
   numérotées et le nom qu'on a envoyé - preuve la plus forte possible, le firmware affiche
   littéralement ce qu'on lui a envoyé.
4. **Rendu audio réel** : note jouée sur le canal MIDI d'une Part → signal non nul mesuré
   (peak/RMS) - le son sort vraiment, pas seulement l'affichage.
5. **Hypothèse de mapping des banques vérifiée** : les catégories de ce projet "Internal A"/
   "Internal B" (patchInfos[] index 0-63 / 64-127, lues directement en ROM) sont exactement les
   vraies banques **Preset A** (`bank=2`) et **Preset B** (`bank=3`) du firmware - confirmé en
   assignant une Part à `bank*64+number` et en retrouvant le nom du patch ROM attendu ("A.Piano 1",
   "Pizzicato") dans la RAM après coup. Une 3e catégorie ROM ("Internal User", index 128-191,
   jamais exposée sous ce nom dans le Browse mais présente dans `patchInfos[]`) s'est révélée être
   la vraie banque **Internal** (`bank=0`, celle qui est réinscriptible sur le vrai hardware) :
   son patch #1 ROM ("JV Strings") correspond exactement au patch par défaut affiché à l'écran de
   boot ("I01:JV Strings"). Résultat : **192 patches + 3 rythmes** (tout le contenu ROM-natif que
   ce projet expose déjà) sont assignables à une Part sans aucun travail supplémentaire.

### Extensions et patches User débloqués (2026-09-08)

**Fait.** Les patches issus des ROMs d'expansion (SR-JV) et les patches "User" sauvegardés via
Save As... sont maintenant assignables aux Parts 1-7 (pas la Part 8/Rhythm - voir plus bas).

La piste initiale ("écrire au format SysEx complet du Patch, `AA=1,BB=numéro+0x40,CC=0x20`,
34+4×116 octets nibblés") s'est avérée inutile. En sondant cette adresse avec juste un marqueur
nom (12 octets ASCII, pas de nibble - voir "Outils" ci-dessous), le slot 0 de cette banque
"Internal Patch Memory" atterrit... exactement à `nvram[0x0d70]` = **l'adresse que ce projet
utilise déjà comme "Patch Temp"** (`setCurrentProgram()`), et le slot 1 exactement `0x16a` (362
octets, la taille d'un `Patch` déjà connue) plus loin. Conclusion : la vraie banque Internal
réinscriptible est un tableau de **64 slots contigus de 0x16a octets à `nvram[0x0d70..0x67f0)`**
- qui se termine exactement là où commence Rhythm Temp (`0x67f0`, déjà connu) - carte mémoire
propre, sans trou. Le "Patch Temp" que ce projet poke déjà directement est donc littéralement le
**slot 0** de cette banque, pas une zone de scratch séparée comme supposé au départ.

Conséquence : écrire un patch personnalisé dans cette banque, c'est le **même `memcpy` direct**
que `setCurrentProgram()` fait déjà pour le slot 0 - aucun SysEx, aucun format nibblé à
retro-ingénierer. `VirtualJVProcessor::injectCustomPatchIntoInternalMemory()`
(`Source/PluginProcessor.cpp`/`.h`) copie les octets bruts du patch (`patchInfos[i].name`, déjà
au format `Patch` de 0x16a octets, expansion ou User) dans `nvram[0x0d70 + slot*0x16a]`, slot =
`partIndex+1` (1 à 7 - **le slot 0 n'est jamais touché**, c'est le patch actuellement actif en
mode Patch simple). La Part pointe ensuite dessus via son champ `patchnumber` normal
(`bank=0, number=slot`), exactement comme pour un patch ROM-natif.

Charge aussi la bonne `waverom_exp` de l'expansion si besoin (même logique que
`setCurrentProgram()`) - avec la même limite que le vrai hardware : **un seul moteur = une seule
extension active à la fois**. Mélanger dans une même Performance des Parts venant de deux
extensions différentes ne sonnera pas juste (celle chargée en dernier gagne) - ce n'est pas une
limite de cette émulation, c'est celle du vrai JV-880 qui n'a physiquement qu'un seul slot
d'extension.

Validé de bout en bout (voir "Outils" ci-dessous) : un patch User fraîchement sauvegardé, assigné
à la Part 3, joue réellement (peak audio non nul) une fois le mode Performance activé. Pas encore
testé avec une vraie extension SR-JV (aucune ne charge dans ce sandbox - fichier présent mais
rejeté, probablement un souci de somme de contrôle/dump sans rapport avec cette fonctionnalité,
pas creusé) mais le chemin de code est strictement identique (même fonction, même copie brute) -
confiance élevée.

`VirtualJVProcessor::isEligibleForPerformancePart()` accepte maintenant tout patch ton (pas
rythme) présent, ROM-natif ou non ; `PatchBrowser` propose donc "Send to Performance Part N"
pour n'importe quel patch de Browse (y compris "User" et les extensions), sauf les rythmes non
ROM-natifs.

**Ce qui reste non supporté** : les **rythmes** d'extension/User pour la Part 8. Le vrai firmware
stocke les rythmes très différemment (`RolandJV880Drum.java` : 61 messages SysEx, un par note,
adresse `AA=1,BB=0x7F,CC=0x40+note`) - pas la même structure simple à un seul bloc que les patches
tons, donc pas la même astuce "trouver l'offset nvram et memcpy" sans plus de travail. Piste de
suite si Alan le demande.

### Architecture du code

- **`Source/PluginProcessor.h`** : `PerformancePart` (nouvelle struct, remplace
  `PerformanceSlot`) - 8 éléments (`kNumPerformanceParts`), légère (pas de copie de patch en
  octets bruts, juste `bank`/`number` + affichage), plus `performanceName[13]`. Plus aucun
  `perfEngines`/`perfScratch` - un seul `mcu`.
- **`Source/PluginProcessor.cpp`** :
  - `sendSysexBlock()` : envoi DT1 bas niveau générique (adresse + N octets de données),
    factorisé depuis l'ancien `sendSysexParamChange()` (qui n'appelle plus que
    `sendSysexBlock(addr, &value, 1)`).
  - `pushPerformanceCommonToEngine()`/`pushPerformancePartToEngine()` : construisent et envoient
    les blocs Common/Part. Les champs effets/voice-reserve du Common sont laissés à 0 (vérifié
    qu'une Part sans voice reserve joue quand même - le voice reserve ne sert qu'à la priorité en
    cas de saturation polyphonique, pas à activer/désactiver une Part).
  - `performancePatchMapping()` (namespace anonyme) : la table de correspondance
    `patchInfos[]` index → `bank`/`number`/`isRhythm` décrite ci-dessus.
  - `setPerformanceModeEnabled()` : bascule juste l'octet Système 0, puis pousse Common + les 8
    Parts si activé - **aucune reconstruction d'un second moteur**, contrairement à l'ancienne
    version.
  - `processBlock()` : simplifié à un seul chemin de rendu (`mcu->updateSC55WithSampleRate()`),
    que le mode Performance soit actif ou non. Seule différence : en mode Patch, chaque message
    MIDI entrant est forcé sur le canal RxCH courant (comme avant) ; en mode Performance, le canal
    d'origine du message est transmis tel quel, et c'est le firmware qui route vers la bonne
    Part selon son `receivechannel` configuré - exactement comme le ferait un vrai câble MIDI
    multitimbral sur un vrai JV-880.
  - Persistance (`.jvpf` / `performance_session.dat`) : format ré-écrit pour 8 Parts légères
    (24 octets/Part + 12 octets de nom, contre l'ancien format qui embarquait jusqu'à 0xa7c
    octets bruts par slot) - un ancien fichier 4-slots ne sera simplement pas reconnu (mauvaise
    taille), sans risque de mauvaise lecture.
- **`Source/ui/PerformanceTab.h/.cpp`** : 8 lignes (au lieu de 4), un champ "Performance Name" en
  haut, pan 0-127 (au lieu de -64..63 - correspond directement au champ `partpan` du firmware).
- **`Source/ui/PatchBrowser.h`** : le clic droit "Send to Performance Part N" ne propose que les
  Parts compatibles (les 7 Parts ton pour un patch classique, la seule Part 8 pour un rythme), et
  seulement si `isEligibleForPerformancePart()` accepte le patch.

### Outils de validation ajoutés (dormants, sans coût si désactivés)

En plus de `JV880_TRACE_NVRAM`/`JV880_SELFTEST_BUTTON` (déjà documentés plus haut) :
- `JV880_SELFTEST_PERF=1` (+ `JV880_SELFTEST_PRE_MS`, `JV880_SELFTEST_PART1_PATCH`,
  `JV880_SELFTEST_PART2_PATCH`) : test bas niveau par DT1 bruts, utilisé pour découvrir/valider
  la carte d'adresses avant d'écrire le code final. Dump `/tmp/jv880_lcd_perf.png`,
  `/tmp/jv880_nvram_perf.bin`, `/tmp/jv880_sram_perf.bin`.
- `JV880_SELFTEST_PERF2=1` : test de bout en bout via la **vraie API publique**
  (`sendPatchToPerformancePart()`, `setPerformanceModeEnabled()`, etc.), donc le meilleur test de
  non-régression pour cette fonctionnalité. Bascule Performance→Patch et vice-versa, vérifie le
  rendu audio à deux notes simultanées sur deux canaux différents, dump
  `/tmp/jv880_lcd_perf2.png` et `/tmp/jv880_lcd_backtopatch.png`.
- `JV880_SELFTEST_PATCHMEM=1` : a servi à localiser la banque "Internal Patch Memory" (voir
  ci-dessus) puis à valider `injectCustomPatchIntoInternalMemory()` de bout en bout - assigne un
  patch d'extension (si une charge dans l'environnement) ou, sinon, un patch User fraîchement
  sauvegardé à la Part 3, active le mode Performance, vérifie le rendu audio. Dump
  `/tmp/jv880_lcd_expansion.png`.

Toutes tournent entièrement headless (aucun device audio ni `$DISPLAY` requis - le hook
s'exécute et `exit(0)` avant toute création de fenêtre), ex. :
```
JV880_SELFTEST_PERF2=1 ./Builds/LinuxMakefile/build/jv880
```

### Interface tab v2 : skin photo (2026-09-08)

> **Superseded** la disposition en grille de `juce::TextButton` décrite juste en dessous
> ("Disposition (2026-09-07...)") - remplacée par un skin basé sur une image, même technique que
> `D110Panel` dans `~/src/D110/d110-vst-emulator` ("the front panel IS the reference
> photograph... every control is an invisible hit-region"). Gardé ci-dessous pour l'historique.

Alan a fourni une image (générée, pas une vraie photo - `/tmp/jv/jv880.png` et sa variante
"compacte" `/tmp/jv/jv880_compact.png`, 2012x304) et a demandé de reprendre la technique du D110.
Seule la version **compacte** est utilisée (`Source/Resources/jv880_panel_compact.png`, embarquée
via `BinaryData::jv880_panel_compact_png` - ajoutée au groupe Assets du `.jucer` avec
`resource="1"`, régénérée par `build/bin/JUCE/Projucer --resave VirtualJV.jucer`, qui recrée aussi
`JuceLibraryCode/BinaryData.{h,cpp}` et les Makefiles - ces dossiers sont gitignorés, aucun risque
de gonfler le repo). La version pleine taille (`jv880.png`, 3280px) n'a pas été intégrée : même
mise à l'échelle, elle ne rentre pas utilement dans la largeur fixe de 820px de cet onglet - à
reprendre seulement si Alan demande un mode plein/compact comme sur D110.

**Comment les coordonnées ont été mesurées** : pas à l'oeil - analyse en composantes connexes
(`scipy.ndimage.label`) sur un masque de seuillage couleur (les boutons sont des rectangles gris
clair ~50 de luminance sur fond ~29, le dial DATA et le bouton VOLUME sont des cercles pleins avec
`fill ≈ π/4` caractéristique d'un disque inscrit dans son rectangle englobant, le LCD est vert/
teal détecté par `g > r + 20`). Un seul passage a trouvé les 12 boutons discrets (rangée du haut :
Patch/Perform, Edit, System, Rhythm, Utility ; rangée du bas : Cursor ◄/►, Tone Select, Mute,
Monitor, Info/Compare, Enter) plus les deux cercles (DATA, VOLUME) sans ambiguïté.

**Différence avec D110Panel** : pas de découpe/incrustation des capuchons de boutons qui
s'enfoncent dans un renfoncement animé - cette technique a besoin d'une vraie photo avec un vrai
renfoncement/ombre à découper, que ce mockup synthétique n'a pas proprement. Le retour visuel
d'appui ici est un simple overlay translucide (rectangle arrondi pour les boutons, anneau pour les
deux molettes) peint par-dessus la photo statique, plus honnête sur le fait que ce n'est pas une
vraie photo de produit.

**DATA** : ni les boutons Data-/Data+ de la v1, ni un dessin figé - le mockup n'a aucun repère
imprimé sur le disque (contrairement à la molette VOLUME du vrai D110, qui en a un et dont il
fallait donc soustraire son propre angle avant rotation), donc un simple glisser vertical
(convention "molette de plugin" : haut = incrémente, bas = décrémente) déclenche des impulsions
`MCU_EncoderTrigger` - une par tranche de `kDialPxPerStep=6` px de glissement total depuis le
`mouseDown`, avec le reliquat conservé pour qu'un glissement lent finisse quand même par
s'enregistrer (même idée que le traitement du VOLUME de `D110Panel`). Le défilement à la molette
(`mouseWheelMove`) marche aussi. Un petit trait indicateur cosmétique tourne de 14° par impulsion
- purement visuel (rien de réel à représenter, ce projet n'expose aucune valeur "position du
dial"), juste pour confirmer que le geste a été pris en compte.

La coche **"Hold DATA while rotating"** (manuel p.49) et le mécanisme `LCD_SendButton`/
`MCU_EncoderTrigger` sous-jacent sont inchangés de la v1 - seule la couche de rendu/hit-test a
changé.

Validé dans Xvfb isolé : capture pendant appui (`mouseDown` maintenu) sur EDIT, VOLUME/PREVIEW et
Cursor ◄ - overlay bien positionné pile sur le bon capuchon à chaque fois ; glissement sur le dial
DATA avec une boucle de petits `xdotool mousemove_relative` (un `xdotool mousemove` en un seul
saut ne génère pas assez d'évènements `MotionNotify` intermédiaires pour que `mouseDrag` de JUCE
les voie tous) - le trait indicateur tourne bien, confirmant que les impulsions `MCU_EncoderTrigger`
partent. Pas moyen de vérifier avec un vrai rendu LCD dans ce sandbox (aucun device audio, donc
`processBlock`/`updateSC55WithSampleRate` ne tourne jamais en usage normal par le GUI - seul le
self-test headless fait tourner l'émulateur manuellement) mais le mécanisme d'envoi
(`LCD_SendButton`/`MCU_EncoderTrigger`) est strictement identique à la v1, déjà validée de bout en
bout par `JV880_SELFTEST_BUTTON`/`JV880_SELFTEST_PERF` plus haut - seule la couche UI qui décide
quand les appeler a changé.

**Disposition (2026-09-07, suite au retour d'Alan avec une photo du panneau réel)** : les
boutons sont maintenant rangés dans l'ordre de lecture du vrai panneau (gauche→droite,
haut→bas), pas dans l'ordre de l'enum `MCU_BUTTON_*` : cluster Data Entry Dial en haut (bouton
push du dial + Data -/+), puis rangée Patch/Perform, Edit, System, Rhythm, Utility, puis Cursor
</>, Tone Select, puis Mute, Monitor, Info/Compare, Enter, puis Preview (qui sur le vrai panneau
est en fait la pression du bouton VOLUME, pas un bouton séparé - gardé seul en bas en attendant
un vrai skin). `MCU_BUTTON_DATA` (le bouton-poussoir du dial data lui-même, distinct de sa
rotation) n'a plus de bouton momentané dédié : le manuel d'origine (p.49,
https://cdn.roland.com/assets/media/pdf/JV-880_OM.pdf) documente "hold down and rotate the DATA
dial" comme un geste à part, impossible à reproduire avec un simple clic-relâché - remplacé par
une coche **"Hold DATA while rotating"** qui maintient `MCU_BUTTON_DATA` enfoncé
(`LCD_SendButton(MCU_BUTTON_DATA, 1)`) tant qu'elle est cochée, le temps de cliquer Data -/+.

**À faire plus tard (pas urgent, sur demande d'Alan)** : reprendre cette interface avec un vrai
skin (image du panneau JV-880 en fond, boutons superposés aux vraies positions/formes) plutôt que
la grille de `juce::TextButton` actuelle - purement cosmétique, aucun changement de mécanisme
(`LCD_SendButton`/`MCU_EncoderTrigger` restent les mêmes peu importe le rendu visuel).

**Sources photo repérées (2026-09-08)** : pas de SVG existant trouvé sur le web pour le panneau
JV-880 (cherché - rien chez Roland ni dans la communauté). En revanche
https://www.synthmania.com/jv-880.htm héberge plusieurs photos pleine résolution (~1750×1160,
Canon EOS REBEL T3i) du panneau avant, quasi de face et bien éclairées - `Roland JV-880 002.JPG`
en particulier est un bon candidat de référence pour une vectorisation (voir
`Roland%20JV-880/Images/Roland%20JV-880%20NNN.JPG` sur ce site, NNN = 002 à 008). Pas encore
téléchargées dans le repo ni vectorisées - juste la piste de départ si ce chantier est repris.

**Essai d'auto-trace (2026-09-08)** : test jetable (`convert -edge`/`potrace` sur un crop de
`Roland JV-880 002.JPG`), envoyé à Alan pour inspection puis supprimé (pas dans le repo). Résultat
sans appel : potrace ne récupère que les contours/lignes de la photo (fait pour du line-art, pas
pour une photo avec dégradés et reflets d'aluminium brossé) - pas exploitable comme skin final.
Une vraie illustration vectorielle demanderait un dessin manuel en utilisant la photo comme
référence, pas un auto-trace. Ce chantier reste "à reprendre plus tard sur demande d'Alan", pas de
changement de statut.

## Mode Performance : garde-fou extensions mixtes + bascule On/Off groupée (2026-09-08)

Deux petits ajouts à l'onglet Performance (v2, moteur unique - voir plus haut), suite au retour
d'Alan :

- **Alerte non bloquante sur mélange d'extensions** : `PerformancePart` (`PluginProcessor.h`)
  gagne un champ `expansionI` (même convention que `PatchInfo::expansionI`, `0xff` = ROM-native/
  aucune extension), rempli dans `sendPatchToPerformancePart()` et persisté dans le format
  `.jvpf`/session (`kPerfPartRecordBytes` +1 octet - un ancien fichier au format précédent ne sera
  simplement pas reconnu, même convention que d'habitude). Après chaque assignation, si la Part
  qui vient d'être configurée référence une extension différente de celle d'une autre Part déjà
  présente, une `juce::AlertWindow::showMessageBoxAsync` (non bloquante, juste informative)
  prévient Alan que ce moteur unique ne peut charger qu'une seule extension à la fois - la Part
  assignée en dernier "gagne", les autres joueront le mauvais son. N'empêche jamais l'assignation
  elle-même (c'est un vrai fonctionnement du hardware réel, pas un bug de cette émulation - voir
  déjà le commentaire d'`injectCustomPatchIntoInternalMemory()`).
- **Bouton "All On/Off"** (`PerformanceTab.h`/`.cpp`) : au-dessus de la colonne "On", bascule les 8
  Parts d'un coup - éteint tout si une majorité est déjà allumée, sinon allume tout (jamais un
  clic qui ne change visiblement rien, contrairement à un simple "inverser l'état courant" sur des
  Parts déjà mélangées).

## Configuration des ROM : dossier configurable + interface principale toujours visible (2026-09-08)

Alan a rapporté un échec de chargement des ROM sur une autre machine : la boîte de dialogue
d'erreur ("Cannot load ROMs...Open ROM Folder") s'ouvrait bien, il a copié les fichiers dans le
dossier ouvert, mais ça n'a pas fonctionné - sans indice pour comprendre pourquoi, ni moyen de
réessayer sans redémarrer. Trois changements en réponse :

1. **Dossier ROM configurable et visible** (`rom.h`/`.cpp`, nouvelles fonctions
   `setRomsDirectoryOverride()`/`getRomsDirectoryOverride()`/`getEffectiveRomsDirectory()`, un
   simple `std::string` global dans `rom.cpp` - header sans dépendance JUCE) : par défaut toujours
   le dossier app-data par utilisateur de l'OS (`userApplicationDataDirectory/JV880`, comme avant),
   mais réassignable à n'importe quel dossier. Persisté séparément de `DataToSave` (même raison que
   `keyboardSettingsFile()`) dans `~/.config/JV880/rom_folder.txt` (`romFolderSettingsFile()`/
   `loadPersistedRomFolderOverride()`/`savePersistedRomFolderOverride()` dans `PluginProcessor.cpp`
   - toujours à cet emplacement fixe, indépendamment de ce que pointe l'override lui-même, pour
   rester trouvable même si ce dossier a disparu). Nouveau bloc **"ROM Folder"** dans l'onglet
   Settings (`SettingsTab.h`/`.cpp`) : chemin actuel affiché en clair, statut coloré (vert
   "chargées" / orange "non trouvées ici"), boutons **Browse...** (sélecteur de dossier),
   **Use Default** (retour au dossier par défaut), **Reload ROMs**.
2. **Rapprochement de nom de fichier insensible à la casse** (`rom.cpp::loadRom()`) : si le nom
   exact attendu (ex. `SR-JV80-08 Keyboards of the 60s and 70s - CS 0x3F1E3F0A.BIN`) n'existe pas
   dans le dossier, une recherche de secours parcourt le contenu réel du dossier et matche en
   ignorant la casse - un dump extrait/renommé légèrement différemment (typiquement `.bin` vs
   `.BIN`) échouait silencieusement avant sur un système de fichiers sensible à la casse (Linux),
   avec exactement le même symptôme que "aucune ROM du tout", sans indice. N'a aucun effet quand le
   nom exact existe déjà (comportement inchangé dans le cas normal).
3. **Rechargement sans redémarrer, interface principale toujours visible** : la boîte de dialogue
   modale bloquante ("Cannot load ROMs...") a disparu - `VirtualJVEditor` affiche maintenant
   toujours sa fenêtre principale, avec un jeu d'onglets réduit à **Settings seul**
   (`showRomSetupOnly()`) tant que `processor.loaded` est faux, plutôt qu'une alerte OS et une
   fenêtre par ailleurs vide. Le constructeur de `VirtualJVProcessor` a été refactoré :
   `attemptLoadRoms()` (nouvelle méthode privée) contient tout ce qui, avant, s'exécutait
   inconditionnellement après `preloadAll()` (démarrage du moteur, construction de `patchInfos[]`,
   patches User, banque Performance, etc. - voir son propre commentaire dans `PluginProcessor.h`),
   rejouable à l'identique. `retryLoadRoms()` (méthode publique) rappelle `attemptLoadRoms()` puis,
   si ça réussit cette fois, prévient l'éditeur actif via `VirtualJVEditor::romsBecameAvailable()`
   (même convention que `updatePerformanceTab()`) qui reconstruit le vrai jeu d'onglets en place -
   aucun redémarrage de l'appli/du host nécessaire. **Volontairement limité au cas "jamais chargé
   avec succès"** (gardé par `if (loaded) return...` à l'entrée d'`attemptLoadRoms()`/
   `retryLoadRoms()`) : changer de dossier ROM une fois le moteur déjà démarré et en cours d'usage
   n'est pas supporté (patchInfos[] contient des pointeurs bruts vers `loadedRoms`/
   `expansionsDescr`, le thread audio peut lire `mcu` en parallèle) - `setRomsFolderOverride()`
   persiste quand même le nouveau choix pour le prochain lancement dans ce cas, mais ne touche pas
   au moteur déjà en marche.

   Validé de bout en bout dans Xvfb isolé (voir la convention de test habituelle) : lancement avec
   un dossier ROM vide → fenêtre principale visible, seul l'onglet Settings présent, message
   orange "ROM files not found here...", chemin affiché correctement ; copie des 5 fichiers ROM
   requis dans ce même dossier pendant que l'appli tourne, puis clic sur "Reload ROMs" → tous les
   onglets (Browse/Performance/Common/Tone 1-4/Settings/Interface) apparaissent immédiatement,
   patches listés, sans redémarrer le processus. Vérifié aussi que le menu contextuel Browse et
   l'assignation Performance normale fonctionnent après ce rechargement à chaud, et qu'assigner
   deux patches ROM-natifs (donc `expansionI=0xff` tous les deux) à deux Parts ne déclenche pas
   l'alerte extensions-mixtes (faux positif évité).
