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

### Ce qui n'est PAS encore assignable à une Part

Les patches issus des ROMs d'expansion (Card/SR-JV) et les patches "User" sauvegardés via Save
As... (`patchInfos[]` index ≥ 195) ne sont **pas** assignables à une Part de Performance : le
firmware ne référence jamais les données d'un patch en clair depuis une Part, seulement un
numéro dans sa propre mémoire de patches (Patch Memory) - il faudrait donc *aussi* écrire ces
patches dans la vraie banque Internal réinscriptible du firmware (adresse trouvée dans
`RolandJV880.java` : `AA=1,BB=numéro+0x40,CC=0x20` pour l'écriture, format encore différent -
34+4×116 octets avec un layout nibblé propre au format "Patch" complet, pas encore traduit depuis
la structure `dataStructures.h` de ce projet). `PatchBrowser`
(`VirtualJVProcessor::isEligibleForPerformancePart()`) filtre silencieusement ces patches - le
clic droit "Send to Performance Part" n'apparaît simplement pas pour eux. Piste de suite
naturelle si Alan le demande.

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

Les deux tournent entièrement headless (aucun device audio ni `$DISPLAY` requis - le hook
s'exécute et `exit(0)` avant toute création de fenêtre), ex. :
```
JV880_SELFTEST_PERF2=1 ./Builds/LinuxMakefile/build/jv880
```

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
