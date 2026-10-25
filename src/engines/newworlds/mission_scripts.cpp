// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Reluva's mission scripts, written from recordings of the original being
// played (docs/formats/newworlds/play*.md): which events each mission
// reacts to, in the order they happened, and what follows. The messages
// are Reluva's own words for what the game's say; objectives are the
// mission's own strings, by id. Each mission's blocks are in the order the
// recording showed them. Where a recording didn't show something (a fail
// condition never reached, a message missed), it's inferred from the
// objective and fail strings and marked "(inferred)".

#include "engines/newworlds/mission_script.h"

namespace reluva::newworlds {

namespace {

constexpr const char* kScripts = R"(
# --- Rules every mission shares (seen in every recording) -------------------
mission generic F
every built *
  say engineering "{1} finished."
every mine-exhausted
  say engineering "The mine at {a}, {b} has run out of minerals."
every power-low
  say operations "Not enough power: systems are shutting down. Build more generators."
every short-resources
  say operations "Construction held up: not enough resources. Build mines and a resource processor."
every short-crew
  say operations "Production held up: not enough crew. Build accommodation and a transporter pad."
every hub-destroyed *
  say intelligence "The {1} colony hub has been destroyed."
every side-failed *
  say intelligence "The {1} can no longer complete their mission."

# --- Federation ----------------------------------------------------------
mission F1 F
site colony 407 209
site temple 332 152
objective 16
objective 17
objective 18
objective 19 hidden
lose 21 destroyed colony-hub
lose 22 destroyed temple
lose 20 destroyed taubat-building
on start
  say intelligence "Be careful: there is a Klingon colony to the south."
on approach klingon colony
  say intelligence "A Klingon vehicle is heading straight for the colony."
on scanned-nothing science
  say science "The scan is inconclusive. We should try again."
on scanned science taubat-mine
  say science "It looks like a primitive mine, but the radiation there is far too high."
on built-all construction-yard power-generator vehicle-yard accommodation resource-processor
  say command "The colony is established."
  complete 16
on approach science-vehicle taubat-colony
  say medical "We've found where the sickness started. A medical scan of the central buildings should tell us more."
on scanned medical taubat-colony
  say medical "Probably radiation sickness, though Taubat biology is new to us. The sickbay should look into it."
  research plague
on researched plague
  say medical "Confirmed: radiation sickness, made worse by local infections. Something artificial is causing it, and we have to find it."
  complete 17
on scanned engineering temple
  say engineering "The temple houses a neglected reactor with a leaking containment field. A team inside could fix it."
  complete 18
  reveal 19
  say security "One APC should take the temple; anything more will provoke the Taubat."
  say intelligence "The Taubat want us out of the area within five minutes."
  timer taubat-deadline 300
on timer taubat-deadline   # (inferred: the recording's player was in time)
  say intelligence "The Taubat have run out of patience."
  lose 23
on entered temple
  say engineering "The away team is in. Starting the repair."
on captured temple
  say engineering "The containment field is fixed and the radiation is falling."
  complete 19
on all-completed
  say command "All objectives met. Well done."
  win

mission F2 F
objective 14
objective 15
objective 16
objective 17
objective 18
lose 19 destroyed colony-hub
lose 21 destroyed colony
lose 22 destroyed archaeological-site
on built-all construction-yard vehicle-yard accommodation transporter-pad
  say command "The first stage of the colony is complete."
  complete 14
on scanned science archaeological-site
  say science "Scan complete. The data is going to the science block for research."
  research archaeology
on researched archaeology
  complete 16
on approach derelict colony
  say science "A derelict starship of unknown age, possibly Taubat."
  say science "The doctor should run a medical scan on it."
on scanned medical derelict
  say medical "An infectious airborne virus. The sickbay has to start research now."
  research disease
on approach disease colony
  say medical "The infection is spreading. We need a cure, quickly."
on researched disease
  say medical "A cure is ready. Releasing it through the colony's life support."
  complete 17
  complete 18
  say medical "The outbreak is under control."
on stock dilithium 120
  complete 15
on all-completed
  say command "The primary objectives are complete."
  win

mission F3 F
objective 15
objective 16
objective 17
lose 18 destroyed taubat-colony
lose 19 destroyed colony-hub
on built-all power-generator construction-yard vehicle-yard sickbay
  say command "All the required buildings are complete."
  complete 15
on destroyed klingon-colony
  say intelligence "The Klingon colony has been destroyed."
  complete 16
on destroyed romulan-saboteurs   # (inferred)
  complete 17
on all-completed
  say command "The primary objectives are complete."
  win

mission J1 F
objective 21
objective 23
objective 25
lose 22 destroyed colony-hub
lose 24 destroyed temple
lose 26 captured-by-rival temple
on colony-size 3   # three times its starting size (inferred)
  say engineering "The colony's development is complete."
  complete 21
on hub-destroyed klingon
on side-failed klingons
on scanned science radiation-source   # (inferred)
  complete 23
on entered temple
  say engineering "We're inside the Taubat temple."
on captured temple
  say engineering "The alien power source is stable now."
  complete 25
on side-failed romulans
on all-completed
  say intelligence "The Federation has completed its mission."
  win

mission J2 F
site alien-structure 112 280
objective 9
objective 10
lose 11 destroyed colony-hub
lose 12 destroyed alien-structure
on start
  say intelligence "Something is disturbing the colony's shield buffer. Perhaps the magnetic storm."
  say intelligence "Two staff are missing. Their transporter trace leads to an alien structure at {site:alien-structure}. Find them."
  say security "Defend the colony with the best technology we have. Whatever drove the Taubat off this world is strong."
  say security "Photon tanks and turrets on high ground may outrange the Hubrin."
  say intelligence "Build remote mining sites, guarded by shield generators and turrets."
on hub-destroyed klingon
on hub-destroyed romulan
on entered alien-structure
  say intelligence "Searching the building for our people."
on captured alien-structure
  say intelligence "They aren't here. Faint traces say they were moved some time ago."
  complete 9   # (inferred: the recording ends here)

mission J3 F
objective 13
objective 14
objective 15 optional
lose 16 destroyed colony-hub
lose 17 destroyed ruined-city
lose 18 captured-by-rival taubat-devices
on start
  say "Incoming message" "Please help us. Invaders are attacking our world."
  say operations "The Klingons and Romulans aren't here as friends. Be careful."
on scanned science taubat-hangar
  say science "The Taubat hangar is scanned."
  complete 13
on scanned science ruined-city
  say science "The main building of the ruined city is scanned."
  complete 14
  say intelligence "The Taubat have turned against us. We have to fight."
  say "Incoming message" "The Metar are awake now. Beware."
  say "Message from Starfleet" "A starship is on its way to you. Hold out at the colony until it arrives."
  say operations "Other outposts report alien creatures appearing all over the planet."
  timer rescue 490
on timer rescue
  say communications "The starship is in orbit, and calling us."
  win

mission J4 F
objective 12
objective 13
lose 14 destroyed colony-hub
lose 15 destroyed klingon-science-station
lose 16 destroyed science-block
on entered klingon-science-station
  say intelligence "We're in. Searching the building."
on captured klingon-science-station
  say intelligence "We've found the hostages. Beaming them out."
  complete 12
on entered science-block
  say intelligence "We're in. Disconnecting the computer core."
on captured science-block
  say intelligence "The core is disconnected and on its way."
  complete 13
on all-completed
  say intelligence "The Federation has completed its mission."
  win

mission F8 F
objective 13
objective 15
lose 14 destroyed tractor
lose 16 destroyed colony-hub
on start
  say command "The Romulans have beamed down some cloaked tanks for us."
on departed tractor
  say command "The tractor is moving out, with its escort."
on approach hubrin tractor
  say command "Hubrin and Metar units are behaving strangely."
  say command "The tractor is taking a route clear of the Hubrin base."
on approach enemy colony
  say command "Hostile units may be heading for the colony."
on arrived tractor colony
  say command "The tractor is in the colony; getting ready to beam the Metar up."
  say command "The dormant Metar is in our hands."
  complete 13
  research metar
on researched metar
  say science "The research is done. The data shows a biological flux."
  say science "It seems to live partly in subspace, as energy. That may be where its strength comes from."
  complete 15
on all-completed
  say command "The primary objectives are complete. Well done."
  win

mission J6 F
objective 13
objective 15
objective 17
lose 14 destroyed romulan-hub
lose 19 destroyed colony-hub
on scan-started science metar-core
  say science "Scanning now, with a few tricks to clean up the signal."
on scanned science metar-core
  say medical "The Metar biodata is ready, and it's disturbing."
  research metar
on researched metar
  say medical "The change affects every Metar caste. The next ones to hatch will be stronger."
  complete 17
on scan-started engineering romulan-hub
  say engineering "Starting the survey. Now, where would they hide it?"
on scanned engineering romulan-hub
  say engineering "The device is in the Romulan colony hub, but their shields stop the transporter."
on destroyed romulan-shield-generator
  say engineering "Their shields are failing. Trying for a transporter lock."
  complete 15
  complete 13
on all-completed
  say intelligence "The Federation has completed its mission."
  win

mission J7 F
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed hubrin-guide
on side-failed klingons
on stock dilithium 100
  say engineering "We have the dilithium we need."
  complete 13
on side-failed romulans
on arrived hubrin-guide destination   # (inferred)
  complete 15
on all-completed
  say intelligence "The Federation has completed its mission."
  win

mission F12 F
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed containment-device
on built-all vehicle-yard*4 power-generator*5|advanced-generator*3
  say engineering "The colony can now support the containment device. It needs one last round of research."
  complete 13
  research containment-device
on researched containment-device
  say engineering "The device is built. Taking it to the site the Hubrin marked, to the south."
  say intelligence "Another Hubrin says a mistake in the construction has unbalanced the field."
  say engineering "A remote scan confirms it. A science vehicle has to get close to isolate the fault."
on scanned science containment-device
  say engineering "The fault is found. Recalibrating the device."
  complete 15
on all-completed
  say engineering "Done. Left as it was, it would have burnt out our whole power grid."
  win

mission J8 F
objective 13
objective 15
objective 17
lose 14 destroyed colony-hub
lose 16 destroyed vehicle-yard
lose 18 destroyed anomaly-device
on researched anomaly-device   # (inferred from the Klingon side)
  complete 13
  complete 15
on arrived anomaly-device target
  complete 17
on all-completed
  win

mission F14 F
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed containment-device
on scanned science metar-core
  say science "These readings are strange... they look like the signals of the anomaly generator."
  complete 13
  complete 15
on all-completed
  say intelligence "All objectives met. Well done."
  win

mission J9 F
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed metar-cores
on scan-started engineering metar-temple
  say engineering "Scanning for the anomaly device's technology."
on scanned engineering metar-temple
  say engineering "We've beamed the technology to the colony hub for research."
  research anomaly-device
on researched anomaly-device
  complete 13
on captured metar-starship
  say engineering "The starship is ours."
  complete 15
on all-completed
  win

mission C9 F
objective 60
lose 61 destroyed colony-hub
on start
  say communications "We've lost the transmission. Their communications may have been knocked out, or..."
  say security "There are huge energy readings to the north. An Orion force that size means an invasion."
  say "Incoming message" "Give up the colony, or lose it and your people with it."
on destroyed orion-forces   # (inferred)
  complete 60
on all-completed
  win

# --- Klingon ---------------------------------------------------------------
mission K1 K
site lookout 271 89
site factory 372 115
site temple 372 107
objective 15
objective 16
objective 17
objective 19
objective 20
objective 22 optional
objective 24 optional
lose 25 destroyed colony-hub
on start
  say operations "Welcome to Taal, commander."
on built-all vehicle-yard power-generator construction-yard resource-processor mining-station
  complete 15
  complete 16
on built-all colony-hub-upgrade disruptor-turret
  complete 17
on approach taubat colony
  say "Incoming message" "This land is holy to us. Go away!"
on approach * lookout
  say intelligence "A Taubat lookout post at {site:lookout}."
on scanned science fortress
  say science "These ruins were the home of an ancient race: the Hubrin."
  complete 20
on approach * factory
  say intelligence "The Taubat vehicle factory is at {site:factory}."
  say intelligence "Their temple is at {site:temple}."
on destroyed factory
  say intelligence "The vehicle factory is destroyed. The Taubat may have no vehicles left."
on captured temple
  say security "The temple is ours. Switching on its mineral transporter."
  say engineering "The minerals are on their way to the colony. We could leave and destroy the temple now."
  complete 24
on destroyed taubat
  say intelligence "The Taubat are no longer a threat."
  complete 19
on all-completed
  say operations "All objectives met."
  win

mission K2 K
site ally-base 279 26
objective 13
objective 14
objective 15
objective 16
objective 17
objective 18 optional
lose 19 destroyed colony-hub
lose 20 destroyed ally-forces
lose 21 destroyed ally-temple
on start
  say "Incoming message" "The rebels are moving. Keep your promise and help us!"
  say intelligence "The allied Taubat base is at {site:ally-base}."
on approach enemy ally-base
  say intelligence "Enemy units are heading for the allied base."
on approach enemy colony
  say intelligence "Enemy units are heading for the colony."
on built-all static-disruptor*5 disruptor-tank*8
  say engineering "The colony's defences are complete."
  complete 13
  complete 14
on scanned science rebel-base
  say intelligence "Orions in the rebel base. They're behind the revolt."
  complete 17
on destroyed rebel-temple
  say intelligence "The rebel temple is destroyed."
on destroyed rebel-factory
  say intelligence "The rebel vehicle factory is destroyed."
on destroyed rebel-houses
  say intelligence "The rebels' houses are destroyed."
on destroyed rebels
  say intelligence "The rebels are crushed."
  complete 16
  complete 15
on all-completed
  say operations "All objectives met."
  win

mission J1 K
objective 20
objective 21
objective 22
lose 23 destroyed colony-hub
lose 24 captured-by-rival temple
on destroyed taubat-settlement
  say security "Victory: the Taubat base is destroyed."
  complete 20
on side-failed federation
on hub-destroyed federation
on captured temple
  say engineering "The temple is ours. Tracing its power source."
  complete 21
on hub-destroyed romulan
on side-failed romulans
on destroyed temple
  say intelligence "The Taubat temple is destroyed."
  complete 22
on all-completed
  say intelligence "The Klingons have completed their mission."
  win

mission J2 K
site alien-structure 112 260
objective 9
objective 10
lose 11 destroyed colony-hub
lose 12 destroyed alien-structure
on start
  say intelligence "Something is disturbing the colony's shield buffer. Perhaps the magnetic storm."
  say intelligence "Two staff are missing. Their transporter trace leads to an alien structure at {site:alien-structure}. Find them."
  say security "Defend the colony with the best technology we have. Whatever drove the Taubat off this world is strong."
  say security "Photon tanks and turrets on high ground may outrange the Hubrin."
on hub-destroyed romulan
on entered alien-structure
  say intelligence "Searching the building for our people."
on captured alien-structure
  say intelligence "They aren't here. Faint traces say they were moved some time ago."
  complete 9   # (inferred: the recording ends here)

mission J3 K
objective 11
objective 12
objective 13
objective 14 optional
lose 15 destroyed colony-hub
lose 16 captured-by-rival taubat-workshops
lose 17 destroyed weapon-target
on start
  say operations "There are targets everywhere, east and south. This planet is busy."
on destroyed taubat-workshop
  say security "A Taubat workshop is destroyed."
on destroyed taubat-workshops
  complete 11
on approach anti-hubrin-weapon central-tower
  say engineering "Too late: a huge charge of energy is building up, aimed at the central tower."
on scanned science weapon-target
  complete 12
  say operations "Other outposts report alien creatures appearing all over the planet."
on destroyed alien-building
  say operations "An alien building is down, and a power surge runs through the planet's core."
on destroyed metar-core
  say operations "The Metar colony core is destroyed."
  complete 13
  say "Message from the High Council" "Hold on: a bird-of-prey is coming for you, if you last that long."
  timer bird-of-prey 970
on timer bird-of-prey
  say "Message from the bird-of-prey" "In orbit now. Stand by to beam up."
  win

mission K7 K
objective 10
objective 11
objective 12
lose 8 destroyed colony-hub
lose 9 destroyed escape-pod
on built-all science-station-upgrade photon-turret*2
  say engineering "The colony expansion is complete."
  complete 10
on approach science-vehicle first-pod
  say intelligence "Life signs. Locking on with the transporter."
  say intelligence "The scientist is safe in the colony."
  complete 11
on approach science-vehicle second-pod
  say intelligence "No life signs here. Scanning for the data core."
  say intelligence "We have the starship's data core."
  complete 12
on all-completed
  say operations "Mission complete."
  win

mission J4 K
objective 15
objective 16
objective 17
objective 18
objective 19
lose 24 destroyed colony-hub
lose 20 destroyed science-station
lose 21 destroyed arsenal
lose 22 destroyed colonial-house
lose 23 destroyed construction-yard
on entered science-station
  say intelligence "The security team is in, clearing out the Metar."
on captured science-station
  say intelligence "The science station is ours again."
  complete 16
on captured colonial-house
  say intelligence "The colonial house is ours again."
on captured construction-yard
  complete 18
on captured arsenal
  say intelligence "The arsenal is ours again."
  complete 17
  complete 15
  say intelligence "The primary objectives are complete."
on hub-destroyed romulan
on side-failed romulans
on hub-destroyed federation
on side-failed federation
on destroyed metar-and-taubat
  say intelligence "Secondary objective met: no Metar or Taubat units left."
  complete 19
on all-completed
  say intelligence "The Klingons have completed their mission."
  win

mission K8 K
objective 12
objective 14
lose 13 destroyed colony-hub
lose 15 survived metar
on destroyed metar-ships-and-portals   # (inferred: the messages weren't shown)
  complete 12
on destroyed metar-three-quarters
  complete 14
on all-completed
  say intelligence "Victory to the Klingons!"
  win

mission J6 K
objective 13
objective 15
objective 17
lose 14 destroyed federation-hub
lose 19 destroyed colony-hub
on scan-started engineering federation-hub
  say engineering "Starting the survey for the Vulcan."
on scanned engineering federation-hub
  say engineering "The Vulcan is in the Federation colony hub, but its shields block the transporter."
on destroyed federation-shield-generator
  say engineering "Their shields are down. Transporting."
  complete 15
  say engineering "We have him."
  complete 13
on scanned science metar-core
  say science "The biodata shows the creatures are mutating. Medical research should study it."
  complete 17
on all-completed
  say intelligence "The Klingons have completed their mission."
  win

mission J7 K
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed hubrin-guide
on side-failed romulans
on stock dilithium 100
  say engineering "We have your dilithium."
  complete 13
on arrived hubrin-guide destination   # (inferred)
  complete 15
on all-completed
  say intelligence "The Klingons have completed their mission."
  win

mission K12 K
objective 12
objective 14
lose 13 destroyed colony-hub
lose 15 destroyed containment-device
on built-all vehicle-yard*4 power-generator*5|advanced-generator*3
  say engineering "The colony can now support the containment device. It needs one last round of research."
  complete 12
  research containment-device
on researched containment-device
  say engineering "The device is built. Taking it to the site the Hubrin marked, to the south."
  say intelligence "Another Hubrin says a mistake in the construction has unbalanced the field."
on scan-started science containment-device
  say science "Trying to isolate the imbalance."
on scanned science containment-device
  say engineering "The fault is found. Recalibrating the device."
  complete 14
on all-completed
  say engineering "Done. Left as it was, it would have burnt out our whole power grid."
  win

mission J8 K
objective 13
objective 15
objective 17
lose 14 destroyed colony-hub
lose 16 destroyed vehicle-yard
lose 18 destroyed anomaly-device
on researched anomaly-device
  say science "Our researchers have worked out how to build the device."
  complete 13
  say engineering "The device is built and on its way. Protect it at all costs."
  complete 15
on arrived anomaly-device target
  say engineering "The device has arrived and is activating. Mission complete."
  complete 17
on all-completed
  win

mission K14 K
objective 12
objective 14
lose 13 destroyed colony-hub
lose 15 destroyed containment-device
on scanned science metar-core
  say science "These readings make no sense... unless the aliens are building an anomaly generator."
  complete 12
  complete 14
on all-completed
  say intelligence "All objectives met. Well done."
  win

mission J9 K
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed metar-cores
on scan-started engineering metar-temple
  say engineering "Scanning for the anomaly device's technology."
on scanned engineering metar-temple
  say engineering "We've beamed the technology to the colony hub for research."
  research anomaly-device
on researched anomaly-device
  say science "Our researchers have worked out how to use the device."
  complete 13
on captured metar-starship
  say engineering "The starship is ours."
  complete 15
on all-completed
  win

mission C10 K
objective 75
objective 76
lose 77 destroyed colony-hub
on destroyed romulan-warbird
  say operations "The warbird is destroyed. A fine victory, commander."
  complete 76
on hub-destroyed romulan
  say intelligence "The Romulan colony is in ruins."
on destroyed taubat-colony
  say intelligence "Every Taubat building has fallen to us."
  complete 75
on all-completed
  win

# --- Romulan ---------------------------------------------------------------
mission R1 R
site rival-base 165 323
site alien-building 143 260
objective 13
objective 16
objective 17
objective 18 optional
objective 19 optional
objective 20 optional
lose 21 destroyed colony-hub
on start
  say operations "Welcome, commander. The staff are entirely at your service."
on destroyed rival-cloaked-tanks
  say intelligence "The three rival cloaked tanks are destroyed."
  complete 16
on built-all construction-yard vehicle-yard resource-processor power-generator|advanced-generator accommodation
  say engineering "Every building we were asked for is finished."
  complete 13
on approach * rival-base
  say intelligence "The rival base is at {site:rival-base}."
on destroyed rival-power-generator
  say intelligence "One of the rivals' power generators is down."
on destroyed rival-base
  say intelligence "The rival base is destroyed. We should find out who was behind it."
  complete 19
  research rivals
on destroyed rival-tanks
  say intelligence "None of the rival tanks are left."
on researched rivals
  say science "The work at the site looks like Admiral Narok's."
  complete 20
on approach * alien-building
  say intelligence "An alien building at {site:alien-building}. We should find out what it's for."
on scanned science alien-building   # (inferred: the scan before the research wasn't shown)
  research temple
on researched temple
  say science "The Taubat temple seems to feed a stasis field under four groups of ruins to the north."
  complete 17
on all-completed
  say operations "Mission complete."
  win

mission R2 R
site taubat-hangar 268 154
site taubat-temple 268 156
objective 13
objective 14
objective 15
objective 16 optional
objective 17 optional   # not in the COMMAND panel's list, though the prisoners were freed
lose 18 destroyed colony-hub
on start
  say operations "Welcome to Taphet. All is in order."
on approach enemy colony
  say intelligence "Enemy units are closing on the colony."
on built-all construction-yard vehicle-yard science-station sickbay
  say engineering "Every building we were asked for is finished."
  complete 13
on approach * taubat-hangar
  say intelligence "A Taubat vehicle factory at {site:taubat-hangar}."
on approach * taubat-temple
  say intelligence "A Taubat temple at {site:taubat-temple}."
on captured taubat-temple
  say security "The temple is ours. It can transport minerals; switching it on."
  say engineering "The minerals are on their way to the colony. We could pull out and destroy the temple."
  complete 16
on captured taubat-hangar
  say security "The vehicle factory is ours. Searching for the Taubat prisoners."
  say security "The prisoners are safe in the colony. We could pull out and destroy the factory."
  complete 17
on stock dilithium 50
  say communications "Beaming 50 units of dilithium up to the starship."
  complete 15
on destroyed taubat-temple
  say intelligence "The Taubat temple is destroyed."
on destroyed orion-hangar
  say intelligence "One of the Orions' vehicle factories is destroyed."
  timer orion-threat 240   # (inferred: the taunt came about four minutes later; its cause wasn't seen)
on timer orion-threat
  say "Incoming message" "You Romulans will pay for meddling in our affairs!"
  say communications "The Orions. An empty threat."
on destroyed orion-houses
  say intelligence "The Orions' houses are destroyed."
on destroyed orions
  say intelligence "No Orion or Taubat units are left."
  complete 14
on all-completed
  say operations "Mission complete."
  win

mission J1 R
objective 21
objective 23
objective 25
lose 22 destroyed colony-hub
lose 24 captured-by-rival temple
lose 26 destroyed temple
on start   # (the recording showed it some minutes in; what set it off isn't known)
  say intelligence "The Federation is building up a large force on the planet."
on hub-destroyed klingon
on side-failed klingons
on hub-destroyed federation
on side-failed federation
on approach science-vehicle temple
  say intelligence "This building is the radiation source. Its containment field is out of line and has to be fixed from inside."
  complete 23
on captured taubat-settlement
  say security "The raid on the Taubat base worked."
  complete 21
on captured temple
  say engineering "The Taubat temple is ours."
  timer reactor 80   # the repair, approximated as a wait (80 s in the recording)
on timer reactor
  say engineering "The alien power source is under our control."
  complete 25
on all-completed
  say intelligence "The Romulans have completed their mission."
  win

mission J2 R
site alien-structure 112 280
objective 7
objective 8
lose 9 destroyed colony-hub
lose 10 destroyed alien-structure
on start
  say intelligence "Something is disturbing the colony's shield buffer. Perhaps the magnetic storm."
  say intelligence "Two staff are missing. Their transporter trace leads to an alien structure at {site:alien-structure}. Get them back."
  say security "Guard the colony with our best technology. Anything this strong explains why the Taubat left."
  say security "Photon tanks and turrets on the heights may match the Hubrin's reach."
  say intelligence "Build remote mining sites, guarded by shield generators and turrets."
on hub-destroyed klingon
on hub-destroyed federation
on entered alien-structure
  say intelligence "Searching the building for our people."
on captured alien-structure
  say intelligence "They aren't here. Faint traces say they were moved some time ago."
  complete 8   # (inferred: the recording ends here)
  say communications "New orders: the colony staff go to a new outpost, and another team will look for the missing."

mission J3 R
objective 11
objective 12
objective 13 optional   # not in the mission report
lose 14 destroyed colony-hub
lose 15 captured-by-rival taubat-workshops
lose 16 destroyed weapon-target
on start
  say operations "Taubat activity is rising. Many units are coming in from the north."
every entered taubat-workshop
  say operations "Infiltration teams are going in. Stand by."
every captured taubat-workshop
  say operations "Our forces hold a Taubat workshop."
on captured taubat-workshops
  complete 11
  say engineering "The harmonic generators are cut off, but a cascade discharge has started and can't be stopped."
on scan-started science weapon-target
  say science "Scanners running."
on scanned science weapon-target
  say science "Confirmed: this is the harmonic field's focal point. Now the energy is off the scale, and fissures are opening."
  complete 12
  say "Incoming message" "The Metar sleep no longer. They will see you."
  say "Message from the Senate" "Fall back to the colony and hold it. A starship is coming for the alien data, and for you."
  say operations "Unknown life forms everywhere, and energy readings off the scale."
  say operations "Other outposts report the aliens coming up all over the planet."
  timer rescue-signal 486
  timer pick-up 1015
on timer rescue-signal
  say communications "The Senate's rescue craft is calling: interference stops it beaming us up yet. Stand by."
on destroyed alien-building
  say operations "An alien building is down, and a power surge runs through the planet's core."
on timer pick-up
  say "Incoming message" "Send us all the alien data now. We'll collect you once it's in."
  win

mission J4 R
objective 12
lose 13 destroyed klingon-security-center
lose 14 destroyed colony-hub
on entered klingon-arsenal
  say intelligence "The team is in, searching the building."
on captured klingon-arsenal
  say intelligence "The arsenal is ours. Setting up the anti-matter fields."
  timer anti-matter 76   # holding it until they're set up (76 s in the recording)
on hub-destroyed federation
on side-failed federation
on timer anti-matter
  complete 12
on all-completed
  say intelligence "The Romulans have completed their mission."
  win

mission R9 R
objective 12
objective 14
objective 16
lose 13 destroyed federation-science-block   # (inferred: "not held", approximated as lost to destruction)
lose 15 destroyed alien-device
lose 17 destroyed colony-hub
on entered federation-science-block
  say intelligence "The team is in, searching the building."
on captured federation-science-block
  say intelligence "We have the device data. Sending it now."
  complete 12
  research alien-device
on researched alien-device
  say engineering "The study is finished. Construction of the device is under way."
  complete 14
on arrived alien-device klingon-colony
  say security "The device is inside the Klingon colony. Stand by for activation."
on destroyed klingon-colony   # (inferred: the blast that followed)
  complete 16
on all-completed
  say intelligence "The Romulans have completed their mission."
  win

mission J6 R
objective 13
objective 15
objective 17
lose 14 destroyed klingon-hub
lose 19 destroyed colony-hub
on scan-started science metar-core
  say science "Running a thorough scan."
on scanned science metar-core
  say science "It worked. The creatures are adapting themselves. Medical should study the data."
  complete 17
on hub-destroyed federation
on side-failed federation   # (inferred: a message the panel hid)
  say engineering "Starting the survey. Looking for a likely target."
on scanned engineering klingon-hub
  say engineering "Their council chamber should suit the test nicely. Waiting for their shields to drop."
  complete 13
on destroyed klingon-shield-generator
  say engineering "The Klingon shields have collapsed. Transporting."
  complete 15
# The device's test then takes out the Klingon hub (the generic rule
# reports it); the recording's end came after it, here with the last
# objective.
on all-completed
  say intelligence "The Romulans have completed their mission."
  win

mission J7 R
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed hubrin-guide
on side-failed klingons
on stock dilithium 100
  say engineering "The dilithium quota is met."
on hub-destroyed federation
on side-failed federation
on stock transalumin 50   # (inferred: a message the panel hid; both quotas are needed, met here in the recording's order)
  complete 13
on arrived hubrin-guide destination   # (inferred)
  complete 15
on all-completed
  say intelligence "The Romulans have completed their mission."
  win

mission R12 R
objective 12
objective 14
lose 13 destroyed colony-hub
lose 15 destroyed containment-device
on destroyed hubrin-observer
  say intelligence "The Hubrin observer has been killed."
on built-all vehicle-yard*4 power-generator*5|advanced-generator*3
  say engineering "The colony can now support the containment device. It needs one last round of research."
  complete 12
  research containment-device
on researched containment-device
  say engineering "The device is built. Taking it to the site the Hubrin marked, to the south."
  say engineering "Transport done: the field device is in place."
  say intelligence "Another Hubrin by the device says a building error has unbalanced its field."
  say engineering "A remote scan agrees. A science vehicle has to get close to isolate the fault."
on scan-started science containment-device
  say science "Trying to isolate the imbalance. Stand by."
on scanned science containment-device
  say engineering "Found the error. Recalibrating the device."
  complete 14
on all-completed
  timer settled 465   # the end came about eight minutes after the fix; why wasn't seen
on timer settled
  say engineering "A close call: set up as it was, it would have overloaded the whole system."
  win

mission J8 R
objective 13
objective 15
objective 17
lose 14 destroyed colony-hub
lose 16 destroyed vehicle-yard
lose 18 destroyed anomaly-device
on researched anomaly-device
  say science "Our researchers have found how to make the device."
  complete 13
  say engineering "The device is built and launched. Protect it whatever the cost."
  complete 15
on arrived anomaly-device target
  say engineering "The device has reached its target and is activating. Mission complete."
  complete 17
on all-completed
  win

mission R14 R
objective 12
objective 14
lose 13 destroyed colony-hub
lose 15 destroyed containment-device
on scanned science metar-core
  say science "The readings point one way: the Metar are building an anomaly generator."
  complete 12
  complete 14
on all-completed
  say intelligence "Mission complete."
  win

mission J9 R
objective 13
objective 15
lose 14 destroyed colony-hub
lose 16 destroyed metar-cores
lose 17 destroyed metar-temple   # (inferred)
on scan-started engineering metar-temple
  say engineering "Scanning for the anomaly device's technology."
on scanned engineering metar-temple
  say engineering "The technology we need is beamed back to the colony hub for study."
  research anomaly-device
on researched anomaly-device
  say science "Our researchers have found how to use the device's technology."
  complete 13
on captured metar-starship
  say engineering "The starship is under our control."
  complete 15
on all-completed
  win

mission C11 R
objective 81
objective 82
objective 83
objective 84
objective 85
lose 86 destroyed colony-hub
lose 87 arrived rival-starships   # (inferred: no countdown was seen)
on start
  say "Message from Narok" "Be quick: Federation and Klingon ships will soon come looking for their missing people."
on approach * first-pod
  say operations "We've found a pod. A science scan should tell us more."
on scanned science first-pod
  say science "The Klingon ambassador isn't here, but the area is full of Orion and Taubat energy traces."
  timer narok-warning 50
on scanned-nothing science
  say science "The scan failed. We'll have to try again."
on timer narok-warning
  say "Message from Narok" "Find the man, or you'll never leave Junil IV."
on approach * second-pod
  say operations "Another of the Babarakka's escape pods. A medical scan will show who was in it."
on scanned medical second-pod
  say medical "It's empty, but a human was aboard. There are strong bi-phasic traces too, like the Hubrin's."
  complete 81
  say "Message from Narok" "The Hubrin got there first. Find their fortress and bargain for the ambassador. Quickly."
on approach * temple
  say operations "A medical scan of the temple would give us a transporter lock on the ambassador."
  complete 82   # (inferred: when he counts as located wasn't seen)
on scan-started medical temple
  say operations "Locking on. Stand by."
on scanned medical temple
  say operations "We have the ambassador."
  complete 84
  say "Message from Narok" "Now crush them with the Empire's might."
on destroyed orion-taubat-structures   # (inferred: the message the panel hid)
  complete 85
  say "Message from Narok" "Well done. Have the ambassador ready; his ship is on its way, and you'll be reassigned."
on approach * hubrin-fortress
  say communications "We've found the Hubrin, their fortress built on the ruins of a Metar hive. They're hailing us."
  say "Transmission from the Hubrin" "Why does the stranger come near?"
  say "Message from Narok" "Hand over the Federation man. He is a criminal of the Empire."
  say "Transmission from the Hubrin" "We don't believe you. The human goes home under our protection."
  say communications "The Hubrin have stopped answering."
  say "Message from Narok" "Then kill the ambassador, wipe out the Hubrin and level their fortress!"
on destroyed hubrin-fortress   # the Federation man died with it: the objective is approximated as met
  say "Message from Narok" "Good. The Federation man is dead, and the fortress with him."
  complete 83
on all-completed
  say command "All objectives complete."
  win
)";

} // namespace

const std::vector<MissionScript>& missionScripts() {
    static const std::vector<MissionScript> scripts = [] {
        std::vector<MissionScript> s;
        if (Error err = parseMissionScripts(kScripts, s))
            s.clear(); // the tests parse it, so this doesn't happen
        return s;
    }();
    return scripts;
}

} // namespace reluva::newworlds
