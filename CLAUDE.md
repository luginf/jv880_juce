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

## Affichage principal en photo du panneau : PanelSkin, modes Compact/Full (2026-09-08)

> **Superseded** : la section "Interface tab v2 : skin photo" ci-dessus décrivait un skin propre
> à l'onglet Interface, construit directement dedans (`InterfaceTab` possédait tout son propre
> code de rendu/hit-test). Le mécanisme de rendu/hit-test a depuis été extrait dans un composant
> partagé, `PanelSkin` (voir ci-dessous) - `InterfaceTab` n'est plus qu'un hôte fin autour de ce
> composant. Les coordonnées mesurées restent les mêmes (juste transposées dans le repère
> "full" ci-dessous), gardé pour l'historique/le contexte de la découverte initiale.

Branche `panel` (créée depuis `main` après fast-forward de `performance`, qui contenait tout ce
qui précède dans ce fichier). Alan a demandé de reprendre le principe du skin photo du D110 mais
pour la fenêtre principale entière, pas seulement l'onglet Interface : un panneau complet
(photo pleine taille) ou compact au choix, **à la place du LCD actuel, tout en haut**, avec ce
LCD réellement intégré dans la photo (comme D110Panel) - plus une option "LCD seul" (comportement
historique inchangé) dans Settings, et un panneau de commandes ultra-compact (boutons seuls, sans
LCD/Volume) dans l'onglet Interface pour ceux qui gardent "LCD seul" en haut.

### Découverte clé : les trois images sont des recadrages exacts d'une seule et même image

Avant d'écrire une seule ligne de layout, comparaison directe en `numpy` des trois PNG fournis
(`jv880.png` 3280x304, `jv880_panel_compact.png` 2012x304, `jv880_compact_commands.png`/
`jv880_commands.png` 948x304) : `jv880_panel_compact.png` == `jv880.png[:, 382:2394]` et
`jv880_commands.png` == `jv880.png[:, 1435:2383]`, diff moyen et max **exactement 0** dans les
deux cas (pas une similarité approximative - des recadrages pixel pour pixel). Donc une seule
table de coordonnées mesurées (boutons, centres/rayons des deux molettes, rectangle de la vitre
LCD - même technique d'analyse en composantes connexes `scipy.ndimage.label` que pour le premier
skin), dans le repère de `jv880.png` (3280x304, `PanelSkin::kMasterRefW/kMasterRefH`), et chaque
variante n'est qu'une fenêtre (offset + largeur) sur cette même table :
- `Variant::kFull` : offset 0, largeur 3280 (tout, y compris PHONES/PCM CARD/DATA CARD/MIDI
  MESSAGE/POWER - décoratifs, aucune zone cliquable dessus).
- `Variant::kCompact` : offset 382, largeur 2012 (VOLUME/LCD/DATA/boutons, sans les à-côtés).
- `Variant::kCommands` : offset 1435, largeur 948 (DATA + les 12 boutons seulement, ni LCD ni
  VOLUME - pour l'onglet Interface, voir plus bas).

### `Source/ui/PanelSkin.h/.cpp`

Composant unique réutilisé à deux endroits (fenêtre principale ET onglet Interface) - remplace
l'implémentation dupliquée du skin v2. `PanelSkin::Variant` porte l'image (`BinaryData::*`),
l'offset/largeur de fenêtrage ci-dessus, et `hasLcdAndVolume` (faux uniquement pour `kCommands`).
`setVariant()` permet de changer d'image/repère à chaud (Settings togglant Compact ↔ Full) sans
reconstruire le composant. `heightForWidth(width)` expose la conversion largeur→hauteur
(`kMasterRefH/variant.refW`, homogène - toutes les variantes partagent la même hauteur de
référence 304) pour que l'éditeur puisse réserver la bonne hauteur de bande avant même le premier
`resized()`.

**LCD réellement intégré** (comme D110Panel) : `rebuildLcdImage()` reprend exactement la même
source que `LCDisplay::paint()` (`processor.mcu->lcd.LCD_Update()`, buffer offscreen 1024x1024
dont seuls les 820x100 premiers pixels sont utiles) sur un timer 25Hz, copiée dans une `juce::
Image` possédée par `PanelSkin`. Problème découvert en l'implémentant : le rendu réel de
l'émulateur est 820x100 (ratio 8.2:1) mais l'ouverture LCD mesurée dans la photo fait 656x108
(ratio 6.07:1) - **pas le même ratio**, contrairement à D110 où Alan avait retouché la photo pour
que l'ouverture corresponde exactement au vrai ratio du LCD. Ici, pas de retouche d'image : la
zone est peinte en noir puis le rendu LCD y est dessiné avec `RectanglePlacement::centred` (fit
en préservant le ratio, centré) plutôt qu'étiré - légère marge noire visible sur les bords hauts/
bas dans l'ouverture, qui se lit comme un bezel plutôt que comme une déformation. Compromis
délibéré, pas un défaut non-vu.

**DATA** : identique au premier skin (glisser vertical ou molette souris → impulsions
`MCU_EncoderTrigger`, indicateur cosmétique qui tourne de 14°/impulsion, aucun repère imprimé sur
le mockup donc rien de réel à faire correspondre).

### `VirtualJVProcessor::DisplayMode` / `setDisplayMode()`

`enum class DisplayMode { LcdOnly = 0, PanelCompact = 1, PanelFull = 2 }`, défaut `LcdOnly`
(comportement historique inchangé pour qui ne touche pas au réglage). Persisté dans
`~/.config/JV880/display_mode.txt` (même convention que `rom_folder.txt`/`keyboard_settings.xml`
- surtout PAS dans `DataToSave`, blob à taille fixe). `setDisplayMode()` persiste puis prévient
l'éditeur actif via `VirtualJVEditor::refreshDisplayMode()` (même convention que
`updatePerformanceTab()`/`romsBecameAvailable()`).

### `VirtualJVEditor` : bascule lcd/panelDisplay, limite de largeur relevée

`lcd` (le `LCDisplay` historique, 820x100 fixe) et `panelDisplay` (un `PanelSkin`) coexistent
comme membres ; un seul des deux est visible à la fois selon `processor.displayMode`.
`resized()` calcule `topAreaH` selon le mode - fixe (100) en LcdOnly, ou
`panelDisplay.heightForWidth(getWidth()) + PanelSkin::kControlsRowH` dans les deux modes Panel
(donc `panelDisplay` occupe **toute la largeur de la fenêtre**, contrairement aux onglets qui
restent épinglés à 820px de large comme avant - non demandé de les faire suivre). Alan a
explicitement demandé de retirer la limite de largeur de redimensionnement pour permettre de
voir le panneau complet à une taille lisible : `setResizeLimits` passe de `(820,400,2400,...)` à
`(820,400,6000,...)` - JUCE exige une borne finie, 6000 est large (bien au-delà même d'un panneau
plein 3280px affiché 1:1) plutôt qu'une suppression littérale.

### `InterfaceTab` réduit à un hôte fin autour de `PanelSkin::Variant::kCommands`

Plus de code de rendu/hit-test propre à cet onglet - juste `PanelSkin skin;` configuré avec le
recadrage "commandes" (boutons + DATA, sans LCD/Volume, cohérent avec le fait que le vrai LCD est
déjà visible tout en haut en mode LcdOnly, seul cas où cet onglet a vraiment un intérêt).

### Validé dans Xvfb isolé (fenêtre large, 3400px)

Les 3 modes testés en direct via le sélecteur Settings (pas seulement au premier lancement) :
LcdOnly (comportement identique à avant, capture de référence), Panel Compact (photo + LCD vivant
correctement incrusté, contrôles fonctionnels), Panel Full (idem, à condition d'agrandir la
fenêtre - `xdotool windowsize` à 3300px confirme que la nouvelle limite fonctionne et que le
panneau s'affiche alors en grand, lisible, LCD vivant toujours correctement incrusté). Appui sur
EDIT capturé en pleine résolution dans les deux modes Panel - overlay bien positionné. Onglet
Interface confirmé sur le recadrage "commandes" (DATA + 12 boutons, sans LCD/Volume). Retour à
LcdOnly depuis un mode Panel confirmé (ré-affiche `lcd`, ré-épingle la bande à 820x100).

Pas vérifié : rendu du panneau complet à une largeur de fenêtre inférieure à sa largeur naturelle
(le panneau se contente de rétrécir proportionnellement selon `getWidth()`, donc PCM CARD/DATA
CARD/MIDI MESSAGE/POWER deviennent minuscules mais restent visibles - pas de troncature, juste
une question de lisibilité laissée au choix d'Alan via le redimensionnement de fenêtre).

### Retouches PanelSkin (2026-09-08, suite au premier retour d'Alan sur `panel`)

- **PATCH/PERFORM resynchronise le mode Performance de l'onglet** : `nvram[0x11]` (le flag de
  mode réel du firmware) et `processor.performanceModeEnabled` (le bool géré par l'onglet
  Performance, qui pousse aussi les 8 Parts via SysEx) sont deux choses distinctes - appuyer sur
  le vrai bouton PATCH/PERFORM du panneau ne touchait avant que le premier. `PanelSkin::
  pressButton()` appelle maintenant `processor.setPerformanceModeEnabled(false)` en plus de
  l'appui physique simulé, mais seulement si `performanceModeEnabled` était vrai - remet l'onglet
  Performance en cohérence quand ce bouton sert de raccourci "retour Patch". L'autre sens (Patch
  → Performance via ce même bouton physique) n'a volontairement pas d'équivalent : ce serait un
  Performance "nu" côté firmware, sans les 8 Parts poussées par l'onglet.
- **Onglet Interface renommé "Panel" et masqué hors mode LCD seul** : cet onglet n'a de sens que
  si le vrai LCD est déjà visible ailleurs (en haut, mode `LcdOnly`) - en mode Panel Compact/Full,
  le panneau du haut couvre déjà tout ça. `VirtualJVEditor::showToneOrRhythmEditTabs()` ne l'ajoute
  plus que si `processor.displayMode == LcdOnly` (toujours en dernière position dans les deux
  branches, donc le masquer/l'afficher ne décale jamais l'index d'un autre onglet).
  `refreshDisplayMode()` force `tabsConfiguredForRhythm = -1` avant de rappeler
  `showToneOrRhythmEditTabs()` - sinon son garde-fou anti-reconstruction-inutile (basé uniquement
  sur isRhythm) ignorerait le changement de displayMode.
- **"(manual p.49)" retiré** du texte de la coche - Alan la trouvait superflue une fois la
  fonctionnalité en place.
- **Ctrl+glisser sur DATA = même effet que la coche "Hold DATA while rotating"**, pour la durée du
  geste (`e.mods.isCtrlDown()` lu une fois au `mouseDown`, comme la coche elle-même) - évite
  d'avoir à cocher/décocher pour un geste occasionnel. Anneau autour du dial maintenant à deux
  états : blanc discret pendant un glissement normal, **jaune, plus épais** pendant que
  `MCU_BUTTON_DATA` est réellement tenu (coche OU Ctrl) - distinction demandée par Alan pour voir
  d'un coup d'œil si l'appui réel a lieu.
- **VOLUME devient un vrai réglage de volume** : la rotation (glisser vertical, même convention
  que DATA) pilote maintenant directement `VirtualJVProcessor::setMasterVolume()` (le même volume
  que le slider de l'onglet Settings) au lieu de ne rien faire d'utile - sur le vrai hardware,
  VOLUME est un potentiomètre analogique que le firmware ne voit jamais, seul son PUSH
  (PREVIEW) est numérique. Ce PUSH est donc déplacé sur le **clic droit** (`e.mods.isPopupMenu()`,
  couvre aussi ctrl-clic Mac) plutôt que le clic gauche, qui est maintenant pris par le glissement
  - délibérément non étiqueté sur la photo (demande d'Alan). Un indicateur cosmétique (comme celui
  de DATA, mais reflétant la vraie valeur 0..1 au lieu d'un simple compteur de crans) tourne de
  -135° à +135° pour donner un retour visuel immédiat que ça fonctionne.

Les 4 comportements (resync PATCH/PERFORM, masquage/renommage de l'onglet, anneau DATA à deux
états, VOLUME glisser+clic-droit) validés dans Xvfb isolé : capture de l'anneau jaune pendant un
Ctrl+glisser vs l'anneau blanc discret pendant un glissement normal ; capture du pointeur VOLUME
qui se déplace après un glissement haut puis bas (la valeur réelle change, pas juste l'affichage -
vérifié en comparant les deux captures, le pointeur bouge visiblement) ; clic droit sur VOLUME
confirmé sans effet sur le pointeur (donc sans toucher au volume) ; coche "Enable Performance
Mode" de l'onglet Performance activée puis désactivée automatiquement par un clic sur
PATCH/PERFORM du panneau ; onglet "Panel" présent en mode LCD seul, absent en Panel (Compact).

### LEDs du panneau + clic droit sur le LCD incrusté (2026-09-08)

Alan a montré une capture de référence avec de petites LEDs allumées sur PATCH/PERFORM, EDIT/
SYSTEM/RHYTHM et chaque TONE SWITCH, et a demandé de pouvoir utiliser TONE SWITCH pour couper des
tons ; séparément, a aussi demandé que le clic droit sur le LCD incrusté en mode Panel ouvre le
même sélecteur de couleur qu'en mode LCD seul.

**RE des LEDs TONE SWITCH** (outil dormant ajouté : `JV880_SELFTEST_LED=<ids boutons séparés par
virgule>`, dans `PluginProcessor.cpp` juste après `JV880_SELFTEST_BUTTON` - contrairement à ce
dernier, diffe `nvram`/`sram`/`ram` de façon **incrémentale** (contre l'étape précédente, pas
contre la toute première capture) et dump un screenshot LCD après chaque bouton, pour isoler
précisément ce qui change à chaque pression plutôt qu'un gros diff illisible sur toute la
séquence). Séquence testée : `3,3,5,6,7,11,11,12,13` (MUTE deux fois, MONITOR, COMPARE, ENTER,
EDIT deux fois, SYSTEM, RHYTHM) depuis l'écran Patch Play par défaut. Résultat propre : un seul
octet en `sram` bascule 0x80↔0x00 par bouton, chacun un pas de 0x54 après le précédent :

```
Tone 1 (MUTE)    sram[0x35b2]
Tone 2 (MONITOR) sram[0x3606]
Tone 3 (COMPARE) sram[0x365a]
Tone 4 (ENTER)   sram[0x36ae]
```

0x80 = valeur par défaut au boot (non coupé), 0x00 après une pression. Direction "LED allumée =
coupé" choisie par convention (mixette classique), **pas vérifiée à 100%** contre le vrai
hardware (l'écran Patch Play n'affiche pas de texte d'état par ton pour croiser) - juste le
`&0x80` le plus propre qui n'ait bougé que sur ces 4 boutons précis. `PanelSkin::toneMutedLive()`
lit ça en direct à chaque `paint()` (25 Hz, timer désormais toujours actif - voir plus bas).
`EDIT`/`SYSTEM`/`RHYTHM` en revanche : des dizaines d'octets changent à chaque pression (surtout
le buffer de caractères du LCD qui se redessine pour un nouvel écran), rien d'aussi propre qu'un
simple flag "écran courant" n'en est ressorti - **LEDs non implémentées pour ces trois-là**,
laissées éteintes plutôt que devinées.

**LEDs PATCH/PERFORM** : gratuites en comparaison - `jv880.png` a déjà deux rectangles blancs
vides à cet endroit précis (mesurés par la même analyse en composantes connexes,
`kPatchLedX/Y/W/H` et `kPerformLedX/Y/W/H`), et le flag qui les pilote (`nvram[0x11]`) est déjà
connu à 100% depuis le travail sur le mode Performance v2. Peintes dans la même couleur que le
texte du mot correspondant dans la photo (PATCH orange, PERFORM bleu).

`PanelSkin`'s timer (`startTimerHz(25)`) tourne maintenant **dans toutes les variantes**, pas
seulement celles avec LCD - les LEDs ont besoin d'un rafraîchissement direct partout (y compris
l'onglet "Panel"/`kCommands`, qui n'a pas de LCD mais a bien les boutons TONE SWITCH). Seul
`rebuildLcdImage()` (la copie du bitmap LCD 820x100) reste conditionnelle à `hasLcdAndVolume`,
pour ne pas faire ce travail 25x/seconde sur un onglet qui ne l'affiche jamais.

**Clic droit sur le LCD incrusté** : `LCDisplay::showColorMenu()` (extrait de son ancien
`mouseDown()`, qui l'appelle toujours pour le strip LCD autonome) est maintenant aussi appelable
depuis l'extérieur - `PanelSkin` reçoit un `LCDisplay*` optionnel au constructeur (`nullptr` pour
la variante `kCommands`, qui n'a pas de LCD à cliquer), et son propre `hitTest()` reconnaît
maintenant le rectangle du LCD (`kHitLcd`) : clic droit dessus → `lcdColorMenuOwner->
showColorMenu()`, exactement le même menu, la même liste de couleurs. `VirtualJVEditor` passe son
propre membre `lcd` (le `LCDisplay` toujours présent, même quand il n'est pas affiché) à la
construction de `panelDisplay`.

Validé dans Xvfb isolé : LED PATCH allumée orange au boot (mode Patch par défaut) ; 3 des 4 LEDs
TONE SWITCH allumées rouge au boot pour le patch par défaut chargé (cohérent avec un patch qui
n'utilise réellement qu'un seul de ses 4 tons - preuve indirecte supplémentaire que la lecture
reflète bien un état par ton réel, pas du bruit) ; clic maintenu sur MUTE confirmé tomber
précisément sur le bon bouton (overlay de pression visible). **Non vérifiable dans ce sandbox** :
le vrai basculement live après relâchement du clic, parce qu'aucun device audio n'est disponible
ici et que `processBlock()`/`updateSC55WithSampleRate()` ne tourne donc jamais pendant un usage
GUI normal (seul le self-test headless fait avancer l'émulateur manuellement - voir la note
récurrente ailleurs dans ce fichier) - mécanisme d'envoi du bouton (`LCD_SendButton`) strictement
identique à celui déjà validé pour les 11 autres boutons du panneau. Menu de couleur LCD testé de
bout en bout : clic droit sur le LCD incrusté en Panel Compact → menu affiché avec "Green" coché
→ sélection "Amber" → écran vire ambre → re-testé → retour à "Green" confirmé.

### Deuxième vague de retours PanelSkin : LEDs EDIT/SYSTEM/RHYTHM/UTILITY trouvées, PATCH/PERFORM
### repensé, verrouillage Ctrl-clic façon D110 (2026-09-08)

Suite à des photos du vrai panneau envoyées par Alan en cours de session (LEDs rouges centrées en
haut de EDIT/SYSTEM/RHYTHM/UTILITY et de PATCH/PERFORM, identiques au style TONE SWITCH) et à des
extraits du manuel (comportement de TONE SELECT/PARAM SHIFT, de PATCH/PERFORM) :

- **EDIT/SYSTEM/RHYTHM/UTILITY ont bien une LED**, trouvée cette fois avec succès (contrairement
  au premier essai) - technique : `JV880_SELFTEST_LED` avec chaque bouton pressé **deux fois de
  suite** (comme pour la découverte des LEDs tone-mute), en ne gardant que les offsets qui basculent
  00↔01 dans cette paire précise et nulle part ailleurs sur toute la séquence (EDIT,EDIT,SYSTEM,
  SYSTEM,RHYTHM,RHYTHM,UTILITY,UTILITY) :
  ```
  EDIT     sram[0x00b6]
  SYSTEM   sram[0x00b2]
  RHYTHM   sram[0x00ae]
  UTILITY  sram[0x00bd]
  ```
  Confirmé ensuite avec une séquence non-répétée (EDIT→SYSTEM→RHYTHM→UTILITY→PATCH_PERFORM,
  chaque bouton une seule fois) que le flag d'un bouton s'éteint bien quand on presse un *autre*
  des quatre - exactement le comportement qu'Alan avait décrit de mémoire ("s'éteint lorsqu'on
  change de mode") - confirmé pour EDIT→SYSTEM et SYSTEM→UTILITY précisément.
- **PATCH/PERFORM repensé** : abandon du schéma à deux rectangles colorés (orange/bleu, peints
  sur les rectangles vides déjà présents dans la photo) au profit d'une seule LED rouge centrée,
  même style que toutes les autres - cohérent avec les photos réelles envoyées. Reste piloté par
  `nvram[0x11]`, allumée en mode Patch (texte du manuel : "l'indicateur s'allume lorsque le mode
  de Patch est sélectionné").
- **Toutes les LEDs unifiées** : `PanelSkin::modeLedLit(int)` (index 0-4 dans `kButtons` =
  PATCH/PERFORM, EDIT, SYSTEM, RHYTHM, UTILITY) + `toneMutedLive()` (index 8-11), une seule
  fonction `drawLed()` dans `paint()`, taille/position/couleur identiques pour les neuf - centrée
  au-dessus du bouton plutôt qu'alignée à gauche (correction d'Alan sur le premier jet des LEDs
  tone-mute).
- **Verrouillage Ctrl-clic généralisé, façon D110** (`bool buttonLatched[kNumButtons]`) : un
  Ctrl-clic sur n'importe lequel des 12 boutons de `kButtons` bascule un état "verrouillé" au lieu
  d'un appui momentané - `LCD_SendButton` envoyé une fois à `true`, jamais relâché automatiquement,
  jusqu'au prochain Ctrl-clic sur ce même bouton. Sert les vraies combinaisons documentées dans le
  manuel : tenir TONE SELECT + presser un TONE SWITCH (choisir le Tone à éditer), tenir PARAM SHIFT
  (fonction secondaire de ce même bouton TONE SELECT) + presser -/+ (changer une valeur quel que
  soit le curseur). Surbrillance persistante peinte dans `paint()` indépendamment de
  `pressedButtonIndex`.
- **DATA repensé pour "rester enfoncé" comme le reste** : Ctrl-clic OU clic droit sur le dial
  bascule maintenant directement la coche "Hold DATA while rotating" elle-même
  (`PanelSkin::toggleDataHeld()`), au lieu de ne tenir `MCU_BUTTON_DATA` que pendant la durée d'un
  glissement. L'anneau autour du dial (jaune, épais) reflète maintenant cet état en permanence,
  pas seulement pendant un clic actif - visible même au repos. Simplifie au passage l'ancienne
  logique de gestion d'ordre de pression gauche/droite (glisser-avant-clic-droit), devenue inutile
  puisqu'un simple clic (sans glisser) suffit maintenant à faire basculer l'état.

**Non résolu, mis de côté** : la question d'Alan sur l'accélération du changement de valeur en
tenant DATA pendant la rotation (page 49 du manuel : "des changements plus rapides sont obtenus si
vous pressez la molette tout en la tournant"). Tentative de vérification via un self-test dédié
(`JV880_SELFTEST_DATAACCEL`, toujours dans `PluginProcessor.cpp`, dormant) : forcer le mode Patch,
entrer dans EDIT, avancer le curseur, comparer l'octet du Patch Temp après un tour simple vs un
tour avec DATA tenu - **résultat non concluant**, aucun octet observé n'a bougé dans les deux cas
(le curseur n'a probablement pas atterri sur un champ éditable par la molette avec la séquence de
boutons essayée). Question explicitement dépriorisée par Alan lui-même ("même si on utilisera
probablement plutôt Edisyn pour éditer") - le mécanisme d'envoi (`LCD_SendButton`/
`MCU_EncoderTrigger`) reste strictement le même que pour tout le reste du panneau, donc si le vrai
firmware fait l'accélération, il devrait déjà la faire ici aussi - juste pas vérifié empiriquement.
À reprendre si Alan le redemande explicitement.

Validé dans Xvfb isolé : surbrillance persistante sur TONE SELECT après Ctrl-clic (reste affichée
après relâchement de la souris), disparaît après un second Ctrl-clic ; anneau jaune sur DATA après
un simple Ctrl-clic (sans glisser) apparaît ET la coche "Hold DATA while rotating" se coche toute
seule en même temps (les deux mécanismes bien synchronisés) ; clic droit sur DATA fait disparaître
l'anneau et décoche la case. LEDs EDIT/SYSTEM/RHYTHM/UTILITY et tone-mute non re-vérifiées
visuellement dans ce sandbox (limite déjà documentée : pas de device audio, donc l'état affiché
dépend de l'état de session persisté au lancement, pas d'un vrai tick du firmware déclenché par un
clic) - logique validée uniquement via le self-test headless qui, lui, fait vraiment tourner
l'émulateur.

### Troisième vague : clic droit sur TONE SELECT, position des LEDs, LEDs EDIT/SYSTEM/RHYTHM/
### UTILITY désactivées (clignotement signalé), binaire de dev toujours strippé (2026-09-08)

- **Clic droit sur TONE SELECT** = même bascule verrouillée qu'un Ctrl-clic (spécifique à ce
  bouton, pas généralisé à tous comme pour DATA) - c'est le bouton le plus susceptible d'être
  tenu-pendant-qu'on-en-clique-un-autre en pratique (TONE SELECT+TONE SWITCH, ou sa fonction
  PARAM SHIFT + -/+), donc valait la peine d'un raccourci une-main dédié.
- **Position des LEDs corrigée** : elles flottaient entièrement au-dessus du bouton (dans l'espace
  du texte) - déplacées pour se centrer sur la limite haute du bouton lui-même, majoritairement
  par-dessus, sans dépasser comme avant (`kLedYOffset` repensé : petit décalage vers le bas depuis
  le centrage exact sur le bord, plus un grand espace au-dessus).
- **LEDs EDIT/SYSTEM/RHYTHM/UTILITY redésactivées** : Alan a signalé qu'elles clignotaient de
  façon apparemment aléatoire en usage normal, pas seulement au moment d'appuyer sur leur propre
  bouton. Hypothèse non vérifiée : les octets trouvés (`sram[0xb6]/[0xb2]/[0xae]/[0xbd]`) sont
  peut-être une zone de travail générique que le firmware réutilise aussi pour autre chose une
  fois de vraies voix en cours de jeu (la RE avait été faite en silence, sans note tenue) - donc
  pas les flags dédiés propres qu'ils semblaient être dans ce test plus étroit. `modeLedLit()`
  garde le code pour les indices 1-4 (dormant, non appelé) plutôt que d'être supprimé, au cas où
  cette piste est reprise avec une méthode de RE plus robuste (peut-être en jouant vraiment des
  notes pendant le diff). PATCH/PERFORM (index 0, `nvram[0x11]`) et les 4 LEDs TONE SWITCH
  restent actives - aucun signalement de clignotement sur celles-là.
- **Binaire de dev strippé** : Alan a remarqué que `build/jv880` (104 Mo en Debug, symboles DWARF
  complets non strippés) était très gros comparé à ses ~13 Mo habituels (un build Release). Prendre
  l'habitude de `strip build/jv880` après chaque build Debug utilisé pour du test/dev dans ce
  genre de session, plutôt que de laisser le binaire non strippé traîner.

Validé dans Xvfb isolé : clic droit sur TONE SELECT confirmé basculer la surbrillance persistante
(même effet que le Ctrl-clic déjà testé) ; LED PATCH/PERFORM repositionnée à cheval sur le bord
haut du bouton, plus dans l'espace du texte au-dessus ; aucune LED visible sur EDIT/SYSTEM/RHYTHM/
UTILITY (désactivation confirmée).

### PATCH/PERFORM désynchronisé de l'onglet Performance : corrigé pour de bon (2026-09-08)

Alan a signalé que la coche "Enable Performance Mode" restait quand même parfois désynchronisée du
bouton PATCH/PERFORM du panneau, malgré la correction précédente. Cause : cette correction ne
gérait qu'**un seul sens** (Perform→Patch via le bouton physique désactivait bien le flag, mais
Patch→Perform via ce même bouton ne le mettait jamais à `true`) - documenté comme un choix
délibéré à l'époque ("rien à resynchroniser côté Parts"), mais laissait bien la coche et le
firmware diverger dans l'autre sens.

**Solution retenue** : `PanelSkin::pressButton()` ne fait plus suivre l'appui brut du bouton
PATCH/PERFORM au firmware (`LCD_SendButton`) comme tous les autres boutons - il appelle
directement `processor.setPerformanceModeEnabled(!processor.performanceModeEnabled)` à la place.
Cette fonction envoie déjà le même octet SysEx que déclencherait l'appui physique
(`sendSysexBlock(0, &modeByte, 1)`), donc le firmware finit dans le même état des deux façons -
mais en passant par ce point d'entrée unique, `performanceModeEnabled` (et donc la coche de
l'onglet Performance) ne peut plus jamais diverger de l'état réel du firmware, dans aucun des deux
sens. Simplifie au passage le raisonnement (plus besoin de deux chemins de code séparés pour
Patch→Perform et Perform→Patch).

Validé dans Xvfb isolé, les deux sens : coche décochée → clic sur PATCH/PERFORM → coche cochée
automatiquement ; reclic → coche décochée à nouveau. (Note technique en passant : calibrer les
coordonnées de clic pixel-exactes dans ce sandbox en repérant la position réelle du rectangle vert
du LCD par seuillage couleur - beaucoup plus fiable que d'estimer à l'œil sur une capture réduite,
qui avait fait rater plusieurs clics d'affilée cette fois-ci.)

**Sur les LEDs EDIT/SYSTEM/RHYTHM/UTILITY** : Alan a confirmé que leur clignotement n'était pas
normal, mais que le vrai comportement matériel est qu'elles s'allument sur clic **dans certains
contextes donnés par la ROM** (donc pas un simple flag "bouton pressé" ni "écran courant" tout
court, apparemment plus subtil que ça) - cohérent avec l'hypothèse déjà notée plus haut (les
octets trouvés sont probablement une zone de travail générique réutilisée pour autre chose,
pas les flags dédiés qu'ils semblaient être dans le test RE isolé). Restent désactivées ; piste à
reprendre uniquement si Alan le redemande explicitement, avec une méthode de RE plus poussée
(faire varier le contexte - quel écran, quel paramètre sélectionné - pas juste la séquence de
boutons).

### LEDs EDIT/SYSTEM/RHYTHM/UTILITY : suivi côté application plutôt que lu du firmware, fenêtre
### renommée (2026-09-08)

Alan a retrouvé le passage exact du manuel (procédure SYSTEM, capture jointe) : "Pressez SYSTEM
(l'indicateur s'allume)" - confirmant que c'est un simple **groupe à bascule mutuellement
exclusif** parmi ces quatre boutons (pas un état contextuel compliqué comme supposé après le
clignotement signalé) : cliquer allume la LED et entre dans ce mode, recliquer dessus (ou cliquer
sur un *autre* des quatre) l'éteint et en sort.

Plutôt que de retenter une lecture depuis la RAM du firmware (piste déjà montrée peu fiable - voir
la section précédente), c'est maintenant **suivi entièrement côté application** :
`PanelSkin::activeModeButton` (int, -1 = aucun, sinon l'index dans `kButtons` du bouton actif parmi
EDIT/SYSTEM/RHYTHM/UTILITY), basculé directement dans `mouseDown()` sur un clic simple (bascule si
même bouton, sinon devient le nouveau actif - éteint automatiquement l'ancien). Comme ce composant
est la seule chose qui envoie jamais ces quatre boutons au firmware, ce suivi ne peut pas
clignoter de façon incohérente - au prix d'un risque de dérive si le vrai firmware sort de cet
écran par un autre chemin que ces quatre boutons (accepté comme compromis). Remis à `-1`
explicitement quand PATCH_PERFORM est pressé (`pressButton()`), puisque revenir en Patch/Perform
Play sort forcément de EDIT/SYSTEM/RHYTHM/UTILITY sur le vrai matériel - le seul cas de sortie
indirecte que cette UI peut détecter sans ambiguïté. `modeLedLit()` ne gère plus que l'index 0
(PATCH/PERFORM, toujours lu depuis `nvram[0x11]`, fiable) - les anciennes adresses `sram[0x00b6]/
[0x00b2]/[0x00ae]/[0x00bd]` restent documentées ci-dessus si cette piste RAM est reprise un jour.

**Fenêtre renommée** "VirtualJV" → "Virtual JV-880" (`pluginName`/`pluginDesc` dans le `.jucer`,
qui pilote `JucePlugin_Name` partout - titre de fenêtre Standalone via `getApplicationName()`
dans `juce_audio_plugin_client_Standalone.cpp`, et le nom affiché du plugin VST3/AU/LV2 aussi).
Piège rencontré en le vérifiant : après `Projucer --resave` + `make` incrémental, le binaire
affichait encore l'ancien titre - `include_juce_audio_plugin_client_Standalone.cpp` (qui contient
l'appel à `JucePlugin_Name`) n'avait pas été recompilé alors que son include `JucePluginDefines.h`
avait bien changé (dépendance non détectée par le suivi incrémental du Makefile dans ce cas précis).
Un `rm -rf build/intermediate/Debug` + rebuild complet a résolu ça - réflexe à avoir après tout
changement touchant `pluginName`/`pluginDesc`/etc. dans le `.jucer`, pas seulement un resave.

Validé dans Xvfb isolé : clic sur EDIT allume sa LED, reclic l'éteint, clic sur un troisième bouton
(RHYTHM) après avoir déjà EDIT actif l'éteint et allume RHYTHM à sa place - bascule mutuellement
exclusive confirmée. Titre de fenêtre confirmé "Virtual JV-880" via `xprop WM_NAME`.

### Piste ouverte : synchroniser les Performances internes/presets du vrai JV-880 (2026-09-08)

Alan a demandé de pouvoir récupérer les Performances stockées **dans le JV-880 lui-même**
(16 internes réinscriptibles + 16 Preset A + 16 Preset B en lecture seule, cf. le manuel déjà cité
plus haut dans ce fichier) et de les lister dans notre onglet Performance, séparément de notre
propre banque `.jvpf`. Début d'investigation, **pas terminé** :

- **Mécanisme de chargement confirmé** : le vrai JV-880 n'a pas d'adresse SysEx directe pour lire
  une Performance stockée par numéro - `RolandJV880Multi.java` d'Edisyn (`changePatch()`) montre
  que le firmware la charge d'abord dans la même zone "Performance Temp" déjà utilisée (celle que
  `pushPerformanceCommonToEngine()`/`pushPerformancePartToEngine()` écrivent déjà), via une
  séquence MIDI standard : (1) System byte 0 = 0 (mode Performance, déjà fait par
  `setPerformanceModeEnabled`), (2) **Bank Select MIDI (CC#0)** avec la valeur 80 (Internal/Card)
  ou 81 (Preset A/Preset B), (3) **Program Change MIDI** avec la valeur `number` (Internal/Preset A)
  ou `number+64` (Card/Preset B). Donc : possible d'envoyer ça via `mcu->postMidiSC55()` (déjà
  utilisé partout ailleurs dans ce projet), attendre que ça s'installe, puis relire.
- **Le nom de la Performance atterrit bien dans `sram`**, retrouvé au même endroit à chaque fois
  (`0x206a` dans ce test précis, probablement pas fixe d'une session à l'autre selon ce qui a été
  chargé avant - à vérifier) - cohérent avec l'écriture DT1 déjà validée.
- **Ce qui bloque** : le format de stockage des 8 blocs Part une fois chargés en RAM ne correspond
  **pas** à un simple memcpy des 35 octets envoyés en SysEx (contrairement à Patch Temp, qui EST
  un memcpy direct des 0x16a octets) - une recherche du motif exact des octets 21-30 envoyés pour
  Part 1/Part 2 (canal, patch, niveau, pan...) n'a donné aucune correspondance dans `sram`. Une
  structure répétitive tous les 22 octets a été repérée près du bloc Common (~0x20ba, 0x20d0,
  0x20e6...) qui pourrait être les 8 Parts sous une forme réencodée, mais pas confirmée - les noms
  de patches ROM retrouvés dans le même voisinage (0x2136, 0x22a0) se sont révélés être un faux
  indice (juste la table de patches ROM généraliste, sans rapport avec les Parts de la
  Performance).

**Pas repris plus loin dans cette session** (déjà beaucoup de terrain couvert en une seule
conversation) - la méthode qui a marché à chaque fois jusqu'ici (marqueur distinctif + diff
incrémental un seul champ à la fois, comme pour la découverte des LEDs tone-mute) demande plus de
temps posé que ce qui restait raisonnable à enchaîner d'affilée. À reprendre avec cette même
rigueur si Alan relance le sujet - point de départ : charger une Performance avec une SEULE Part
non vide et tout le reste à zéro, diffé contre une Performance entièrement vide, pour isoler un
seul champ à la fois plutôt que de deviner un layout entier d'un coup.

## DSP Load : piste d'optimisation regardée, LTO activé sur Linux (2026-09-08)

Alan a trouvé le DSP Load (voir la section plus haut) trop élevé et a demandé si une optimisation
était possible. Investigation, toujours sur branche `panel` :

- **`perf` indisponible dans ce sandbox** (machine partagée multi-utilisateurs,
  `perf_event_paranoid=4` - refuse même la lecture d'événements CPU sans capacité root ; pas
  question de toucher ce réglage noyau sur une machine partagée). Profilage par instrumentation
  directe à la place : mesure du temps réel (`/usr/bin/time`) pour rendre N secondes d'audio via
  la boucle self-test headless déjà existante (`updateSC55WithSampleRate()` en boucle serrée, sans
  device audio ni fenêtre) - un proxy fiable pour le coût DSP, comparable avant/après un changement
  donné.
- **Le coût est structurel, pas un gaspillage évident** : `updateSC55WithSampleRate()`
  (`Source/emulator/mcu.cpp`) exécute une instruction MCU réelle par tick à 64kHz
  (`MCU_ReadInstruction()`, cycles fixes "12 par instruction" - commentaire `FIXME` déjà présent,
  hérité de Nuked-SC55, pas quelque chose à changer sans risquer de casser la précision cycle-exacte
  /la justesse audio), plus `PCM_Update()` (synthèse par oversampling), timers, UART. Le dispatch
  des opcodes (`mcu_opcodes.cpp`) passe déjà par une table de pointeurs de fonctions
  (`MCU_Opcode_Table[opcode](...)`), pas une chaîne de `if`/`switch` géante - donc pas de gain
  évident à attendre là sans réécriture (un JIT, hors de portée raisonnable ici). Aucun flush de
  denormals (`_MM_SET_FLUSH_ZERO_MODE`) trouvé nulle part dans le code - piste explorée mais peu
  pertinente ici, le coeur MCU est très majoritairement en entiers, pas en flottants.
- **Vrai gain trouvé, sans risque** : le générateur de Makefile Linux de Projucer/JUCE
  (`jucer_ProjectExport_Make.h:55`, `linkTimeOptimisationValue.setDefault(false)`) désactive la
  Link-Time Optimisation par défaut pour la config Release - contrairement aux exporters
  Xcode/Visual Studio du même projet, qui la laissent à leur propre défaut (`!isDebug()` = activée
  en Release) et en profitent donc déjà sans rien avoir eu à faire. Corrigé en ajoutant
  `linkTimeOptimisation="1"` sur la `<CONFIGURATION>` Release du bloc `<LINUX_MAKE>` dans
  `VirtualJV.jucer` (seul le build Linux Makefile avait besoin de cette correction explicite).
  Mesuré par la méthode ci-dessus (4 paires de mesures entrelacées LTO/non-LTO, même binaire ROM,
  même self-test) : **environ 2-3% de temps réel en moins** pour rendre le même volume d'audio -
  un vrai gain, gratuit, mais modeste (pas le facteur significatif qu'on aurait pu espérer, cohérent
  avec le fait que le coût est déjà structurel comme expliqué ci-dessus, pas de la redondance entre
  fichiers que le LTO aurait pu éliminer). Non-régression vérifiée en rejouant
  `JV880_SELFTEST_PERF2=1` sur le binaire LTO : mêmes peaks audio non nuls, mêmes bascules de mode
  correctes - comportement identique, juste plus rapide. Au passage : `-flto` réduit aussi
  sensiblement la taille des binaires livrés eux-mêmes (Standalone Release ~14 Mo -> ~7,4 Mo avant
  strip, LV2/VST3 `.so` similaires) grâce à l'élimination de code mort inter-fichiers.
- **Piste non retenue** : reconstruire les 4 moteurs MCU en parallèle sur un pool de threads -
  obsolète depuis le passage au mode Performance v2 (un seul moteur, voir plus haut), rien à
  paralléliser dans ce sens-là. Une vraie parallélisation interne au moteur MCU lui-même
  (pipeline CPU/PCM sur des threads séparés) casserait très probablement l'exactitude cycle-à-cycle
  dont dépend la justesse de l'émulation - pas une piste sérieuse pour ce projet.

### `build/build-linux.sh` : strip systématique + suppression de `jv880.a` (2026-09-08)

Alan a remarqué `jv880.a` (l'archive statique intermédiaire "Shared Code" que tous les autres
binaires du build Linux lient déjà - VST3/LV2/Standalone/manifest helpers) traînant dans
`build/` à 222 Mo, à côté d'un Standalone Debug non-strippé à 104 Mo. Le `.deb`/les zips de release
ne l'embarquaient déjà pas (vérifié dans `.github/workflows/main.yml` - seuls `jv880.vst3`,
`jv880.lv2`, le binaire `jv880`/`jv880.linux` et le `.deb` sont zippés/uploadés), donc pas un bug
de release, juste un désagrément du dossier de build local/CI. `build/build-linux.sh` fait
maintenant, après le `make CONFIG=Release` :
- `strip` sur les trois binaires livrés (`build/jv880`, `build/jv880.lv2/jv880.so`,
  `build/jv880.vst3/Contents/x86_64-linux/jv880.so`) - avant, seul `jv880` (Standalone) était
  strippé à la main de temps en temps par habitude documentée plus haut dans ce fichier, jamais les
  deux `.so` du VST3/LV2 alors qu'ils sont tout aussi livrés.
- `rm -f build/jv880.a` - pur intermédiaire de lien, déjà présent dans les trois binaires ci-dessus
  une fois `make` terminé, jamais nécessaire après coup. Sans risque pour un rebuild incrémental
  suivant : `make` le regénère depuis les `.o` déjà en cache s'il en a de nouveau besoin.

Le geste manuel "`strip build/jv880` après un build Debug de dev/test" (déjà documenté plus haut
dans ce fichier) reste une habitude séparée à garder - `build-linux.sh` ne construit que la config
Release et n'est pas ce qu'Alan lance pour un cycle de test rapide en Debug.
