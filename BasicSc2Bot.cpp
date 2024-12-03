#include "BasicSc2Bot.h"
#include <algorithm>
#include <cmath>
#include <sc2api/sc2_typeenums.h>

/*
# Windows
./BasicSc2Bot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map

# Mac
./BasicSc2Bot -c -a zerg -d Hard -m CactusValleyLE.SC2Map
*/

using namespace sc2;

void BasicSc2Bot::OnGameStart() {
	expansions_ = search::CalculateExpansionLocations(Observation(), Query());
	startLocation_ = Observation()->GetStartLocation();
	enemy_base_locations_ = Observation()->GetGameInfo().enemy_start_locations; // Store possible enemy base locations
	current_target_index_ = 0;                                                  // Initialize the target index
}

void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

	if (observation->GetFoodWorkers() < 10) {
		if (TrainUnitFromLarvae(ABILITY_ID::TRAIN_DRONE, 50)) {
			return;
		}
	}

	Units spawning_pools = GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL);
	if (spawning_pools.empty()) {
		if (observation->GetMinerals() >= 200 && observation->GetMinerals() > 1200) { // Attempt to build the Spawning Pool if we have enough minerals
			if (TryBuildStructure(ABILITY_ID::BUILD_SPAWNINGPOOL, UNIT_TYPEID::ZERG_SPAWNINGPOOL, 200)) {
				return;
			} else {
			}
		}
	} else if (spawning_pools.front()->build_progress < 1.0f) { // Wait for spawnning pool to complete
		return;
	}
	if (TryTrainOverlord()) // If overlord trained then return
		return;
	if (TrainArmyUnits()) { // If army units trained then return
		return;
	}

	QueenInjectLarvae();
	TryBuildTechStructuresAndUpgrades();
	AssignWorkersToExtractors();
	BalanceWorkers();

	if (observation->GetMinerals() > 100 && observation->GetFoodWorkers() < 70) { // If enough minerals and worker units not enough
		for (const auto &base : GetActiveBases()) {
			if (base->ideal_harvesters > base->assigned_harvesters) {
				if (TrainUnitFromLarvae(ABILITY_ID::TRAIN_DRONE, 50)) // Train one drone at a time to prevent overproduction
					break;
			}
		}
	}

	// Try to expand if we have less than max_bases and sufficient army units
	const int max_bases = 2;
	Units bases = GetActiveBases();
	if (bases.size() < max_bases && observation->GetMinerals() >= 300) {
		Units combat_units = observation->GetUnits(Unit::Alliance::Self, [](const Unit &unit) { // Check if we have some combat units before expanding
			return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING || unit.unit_type == UNIT_TYPEID::ZERG_ROACH || unit.unit_type == UNIT_TYPEID::ZERG_HYDRALISK ||
			       unit.unit_type == UNIT_TYPEID::ZERG_MUTALISK;
		});
		if (combat_units.size() >= 0) { // Ensure we have a certain amount of combat units before expanding
			if (TryExpand(ABILITY_ID::BUILD_HATCHERY, UNIT_TYPEID::ZERG_DRONE)) {
				return;
			}
		}
	}
	ManageArmy();
	TryUpgradeBase(); // Try to upgrade base
	MorphRoachesToRavagers();
}

void BasicSc2Bot::OnUnitIdle(const Unit *unit) {
	switch (unit->unit_type.ToType()) {
	case UNIT_TYPEID::ZERG_DRONE: {
		const Unit *mineral_target = FindNearestMineralPatch(unit->pos);
		if (mineral_target) {
			Actions()->UnitCommand(unit, ABILITY_ID::SMART, mineral_target);
		}
		break;
	}
	case UNIT_TYPEID::ZERG_QUEEN: { // Queens should inject larvae
		QueenInjectLarvae();
		break;
	}
	case UNIT_TYPEID::ZERG_ZERGLING: // Move combat units to a rally point instead of attacking immediately
	case UNIT_TYPEID::ZERG_ROACH:
	case UNIT_TYPEID::ZERG_HYDRALISK:
	case UNIT_TYPEID::ZERG_MUTALISK:
	case UNIT_TYPEID::ZERG_RAVAGER: {
		Point2D rally_point = GetArmyRallyPoint();
		Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, rally_point);
		break;
	}
	case UNIT_TYPEID::ZERG_SPIRE: { // Research upgrades if not already researching
		if (unit->orders.empty()) {
			Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGFLYERARMORLEVEL1);
			Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGFLYERATTACKLEVEL1);
		}
		break;
	}
	case UNIT_TYPEID::ZERG_HYDRALISKDEN: { // Research upgrades if not already researching
		if (unit->orders.empty()) {
			Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_GROOVEDSPINES);
			Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_MUSCULARAUGMENTS);
		}
		break;
	}
	case UNIT_TYPEID::ZERG_SPAWNINGPOOL: { // Research upgrades if not already researching
		if (unit->orders.empty()) {
			Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST);
			Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGLINGADRENALGLANDS);
		}
		break;
	}
	default:
		break;
	}
}

bool BasicSc2Bot::TrainArmyUnits() {
	bool trained_unit = false;

	Units spawning_pools = GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL);
	Units roach_warrens = GetUnitsOfType(UNIT_TYPEID::ZERG_ROACHWARREN);
	Units hydralisk_dens = GetUnitsOfType(UNIT_TYPEID::ZERG_HYDRALISKDEN);
	Units spires = GetUnitsOfType(UNIT_TYPEID::ZERG_SPIRE);

	int zergling_count = CountUnitType(UNIT_TYPEID::ZERG_ZERGLING); // Counts of existing combat units
	int roach_count = CountUnitType(UNIT_TYPEID::ZERG_ROACH);
	int hydralisk_count = CountUnitType(UNIT_TYPEID::ZERG_HYDRALISK);
	int mutalisk_count = CountUnitType(UNIT_TYPEID::ZERG_MUTALISK);

	const int max_zerglings = 5; // Counts we want
	const int max_roaches = 5;
	const int max_hydralisks = 5;
	const int max_mutalisks = 5;

	// Train combat units based on available tech structures and unit counts
	if (!spawning_pools.empty() && spawning_pools.front()->build_progress == 1.0f && zergling_count < max_zerglings) {
		trained_unit |= TrainUnitFromLarvae(ABILITY_ID::TRAIN_ZERGLING, 50);
	}
	if (!roach_warrens.empty() && roach_warrens.front()->build_progress == 1.0f && roach_count < max_roaches) {
		trained_unit |= TrainUnitFromLarvae(ABILITY_ID::TRAIN_ROACH, 75, 25);
	}
	if (!hydralisk_dens.empty() && hydralisk_dens.front()->build_progress == 1.0f && hydralisk_count < max_hydralisks) {
		trained_unit |= TrainUnitFromLarvae(ABILITY_ID::TRAIN_HYDRALISK, 100, 50);
	}
	if (!spires.empty() && spires.front()->build_progress == 1.0f && mutalisk_count < max_mutalisks) {
		trained_unit |= TrainUnitFromLarvae(ABILITY_ID::TRAIN_MUTALISK, 100, 100);
	}

	return trained_unit;
}

int BasicSc2Bot::CountUnitType(UNIT_TYPEID unit_type) {
	Units units = Observation()->GetUnits(Unit::Alliance::Self, [unit_type](const Unit &unit) { return unit.unit_type == unit_type; });
	return static_cast<int>(units.size());
}

bool BasicSc2Bot::TrainUnitFromLarvae(ABILITY_ID unit_ability, int mineral_cost, int vespene_cost) {
	Units larvae = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);

	for (const auto &larva : larvae) {
		if (Observation()->GetMinerals() >= mineral_cost && Observation()->GetVespene() >= vespene_cost) {
			Actions()->UnitCommand(larva, unit_ability);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildStructure(ABILITY_ID build_structure, UNIT_TYPEID structure_id, int mineral_cost, int vespene_cost) {
	Units existing_structures = GetUnitsOfType(structure_id); // Check if the structure already exists or is under construction
	for (const auto &structure : existing_structures) {
		if (structure->build_progress < 1.0f) { // Already building this structure
			return false;
		}
	}

	if (Observation()->GetMinerals() < mineral_cost || Observation()->GetVespene() < vespene_cost) {
		return false;
	}
	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);
	if (drones.empty()) {
		return false;
	}

	const Unit *drone = nullptr;
	for (const auto &d : drones) { // Find idle drone or drone gathering minerals
		if (d->orders.empty() || d->orders[0].ability_id == ABILITY_ID::HARVEST_GATHER) {
			drone = d;
			break;
		}
	}
	if (!drone) {
		return false;
	}

	Units bases = GetActiveBases();
	const Unit *base = nullptr;
	for (const auto &b : bases) { // Find complete base
		if (b->build_progress == 1.0f) {
			base = b;
			break;
		}
	}
	if (!base) {
		return false;
	}

	Point2D base_position = base->pos;
	const float max_search_radius = 10.0f;    // Maximum radius to search for placement
	const float step_size = 1.0f;             // Step size for expanding the search
	const float min_structure_spacing = 3.0f; // Minimum spacing between structures

	// Search in all directions around the base
	for (float radius = 2.0f; radius <= max_search_radius; radius += step_size) {
		for (float x_offset = -radius; x_offset <= radius; x_offset += step_size) {
			for (float y_offset = -radius; y_offset <= radius; y_offset += step_size) {
				if (sqrt(x_offset * x_offset + y_offset * y_offset) > radius) { // Skip positions that are outside the circular search radius
					continue;
				}
				Point2D test_position = Point2D(base_position.x + x_offset, base_position.y + y_offset);
				bool is_too_close = false;
				for (const Unit *existing_structure : Observation()->GetUnits(Unit::Alliance::Self)) {
					if (DistanceSquared2D(test_position, existing_structure->pos) < min_structure_spacing * min_structure_spacing) {
						is_too_close = true;
						break;
					}
				}

				if (!is_too_close && Query()->Placement(build_structure, test_position)) {
					Actions()->UnitCommand(drone, ABILITY_ID::STOP);
					Actions()->UnitCommand(drone, build_structure, test_position);
					return true;
				}
			}
		}
	}
	return false;
}

Units BasicSc2Bot::GetActiveBases() { // Gets number of active bases
	Units bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);
	bases.insert(bases.end(), lairs.begin(), lairs.end());
	bases.insert(bases.end(), hives.begin(), hives.end());

	Units active_bases; // Filter out bases that are not completed or are destroyed
	for (const auto &base : bases) {
		if (base->build_progress == 1.0f && base->health > 0) {
			active_bases.push_back(base);
		}
	}
	return active_bases;

	return bases;
}

void BasicSc2Bot::TryBuildTechStructuresAndUpgrades() {
	TryBuildVespeneExtractor(); // Build Vespene Extractor if needed
	Units spawning_pools = GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL);

	Units roach_warrens = GetUnitsOfType(UNIT_TYPEID::ZERG_ROACHWARREN);
	if (!spawning_pools.empty() && spawning_pools.front()->build_progress == 1.0f &&
	    roach_warrens.empty()) { // Build roach warren if we have built spawnning pool and no roach warren
		TryBuildStructure(ABILITY_ID::BUILD_ROACHWARREN, UNIT_TYPEID::ZERG_ROACHWARREN, 150);
	}

	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	if (lairs.empty() && !GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY).empty()) { // Try to upgrade base if not lair
		TryUpgradeBase();
	} else if (!lairs.empty() && lairs.front()->build_progress == 1.0f) { // If lair is built, check and make hydralisk den and spire
		Units hydralisk_dens = GetUnitsOfType(UNIT_TYPEID::ZERG_HYDRALISKDEN);
		if (hydralisk_dens.empty()) {
			TryBuildStructure(ABILITY_ID::BUILD_HYDRALISKDEN, UNIT_TYPEID::ZERG_HYDRALISKDEN, 100, 50);
		} else if (hydralisk_dens.front()->build_progress == 1.0f) {
			Units spires = GetUnitsOfType(UNIT_TYPEID::ZERG_SPIRE);
			if (spires.empty()) {
				TryBuildStructure(ABILITY_ID::BUILD_SPIRE, UNIT_TYPEID::ZERG_SPIRE, 200, 150);
			}
		}
	}
}

Point2D BasicSc2Bot::GetArmyRallyPoint() { // Set the rally point of combat units
	Units bases = GetActiveBases();

	if (bases.empty()) {
		return startLocation_;
	}

	float avg_x = 0.0f; // Calculate the average position (center) of all bases
	float avg_y = 0.0f;
	for (const auto &base : bases) {
		avg_x += base->pos.x;
		avg_y += base->pos.y;
	}

	avg_x /= bases.size();
	avg_y /= bases.size();

	return Point2D(avg_x, avg_y);
}

void BasicSc2Bot::BalanceWorkers() { // Balance workers assigned to base
	const ObservationInterface *observation = Observation();
	Units all_bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);
	all_bases.insert(all_bases.end(), lairs.begin(), lairs.end());
	all_bases.insert(all_bases.end(), hives.begin(), hives.end());

	std::vector<const Unit *> undersaturated_bases;
	std::vector<const Unit *> oversaturated_bases;

	for (const auto &base : all_bases) { // Skip incomplete or mined-out bases
		if (base->build_progress < 1.0f || base->ideal_harvesters == 0) {
			continue;
		}

		int workers_needed = base->ideal_harvesters - base->assigned_harvesters;
		if (workers_needed > 0) {
			undersaturated_bases.push_back(base);
		} else if (workers_needed < 0) {
			oversaturated_bases.push_back(base);
		}
	}

	if (undersaturated_bases.empty()) {
		return;
	}

	for (const auto &oversaturated_base : oversaturated_bases) { // Go through all bases and redistribute
		int extra_workers = oversaturated_base->assigned_harvesters - oversaturated_base->ideal_harvesters;
		if (extra_workers <= 0) {
			continue;
		}

		Units workers = observation->GetUnits(Unit::Alliance::Self, [oversaturated_base](const Unit &unit) {
			if (unit.unit_type != UNIT_TYPEID::ZERG_DRONE) {
				return false;
			}
			if (unit.orders.empty()) {
				return false;
			}
			if (DistanceSquared2D(unit.pos, oversaturated_base->pos) < 100.0f) {
				return true;
			}
			return false;
		});

		for (auto &worker : workers) {
			if (extra_workers <= 0) {
				break;
			}
			const Unit *target_base = nullptr;
			float min_distance = std::numeric_limits<float>::max();
			for (const auto &undersaturated_base : undersaturated_bases) {
				float distance = DistanceSquared2D(worker->pos, undersaturated_base->pos);
				if (distance < min_distance) {
					min_distance = distance;
					target_base = undersaturated_base;
				}
			}

			if (target_base) { // Assign workers to undersaturated base
				const Unit *mineral_patch = FindNearestMineralPatch(target_base->pos);
				if (mineral_patch) {
					Actions()->UnitCommand(worker, ABILITY_ID::SMART, mineral_patch);
					extra_workers--;
				}
			}
		}
	}
}

bool BasicSc2Bot::IsCombatUnit(const Unit &unit) {
	return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING || unit.unit_type == UNIT_TYPEID::ZERG_ROACH || unit.unit_type == UNIT_TYPEID::ZERG_HYDRALISK ||
	       unit.unit_type == UNIT_TYPEID::ZERG_MUTALISK || unit.unit_type == UNIT_TYPEID::ZERG_RAVAGER;
}

void BasicSc2Bot::MorphRoachesToRavagers() {
	const ObservationInterface *observation = Observation();

	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);
	if (lairs.empty() && hives.empty()) { // If we dont have lair, return
		return;
	}

	const int ravager_morph_mineral_cost = 25;
	const int ravager_morph_vespene_cost = 75;
	if (observation->GetMinerals() < ravager_morph_mineral_cost || observation->GetVespene() < ravager_morph_vespene_cost) { // If not enough resources, return
		return;
	}

	Units roaches = GetUnitsOfType(UNIT_TYPEID::ZERG_ROACH);

	int ravager_count = CountUnitType(UNIT_TYPEID::ZERG_RAVAGER);
	int desired_ravager_count = 7;
	int morph_count = desired_ravager_count - ravager_count;
	if (morph_count <= 0) {
		return;
	}

	int morphed = 0;
	for (const auto &roach : roaches) { // If roach is idle, morph into ravager
		if (roach->orders.empty()) {
			Actions()->UnitCommand(roach, ABILITY_ID::MORPH_RAVAGER);
			morphed++;
			if (morphed >= morph_count) {
				break;
			}
		}
	}
}

void BasicSc2Bot::ManageArmy() { // Checkpoint to see if army should attack (if we have enough army units)
	if (Observation()->GetArmyCount() > 15) {
		AttackWithArmy();
	}
}

void BasicSc2Bot::AttackWithArmy() { // Send army
	const ObservationInterface *observation = Observation();

	Units combat_units = observation->GetUnits(Unit::Alliance::Self, [](const Unit &unit) { // Get all combat units
		return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING || unit.unit_type == UNIT_TYPEID::ZERG_ROACH || unit.unit_type == UNIT_TYPEID::ZERG_HYDRALISK ||
		       unit.unit_type == UNIT_TYPEID::ZERG_MUTALISK || unit.unit_type == UNIT_TYPEID::ZERG_RAVAGER;
	});

	if (combat_units.empty()) {
		return;
	}

	Units enemy_units = observation->GetUnits(Unit::Alliance::Enemy); // Get enemy units

	if (!enemy_units.empty()) { // If enemy's found, attack closest enemy
		const Unit *target = enemy_units.front();
		for (const auto &unit : combat_units) {
			if (unit->orders.empty()) {
				Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, target->pos);
			}
		}
	} else { // If no enemy's found, attack enemy known home base locations
		if (!enemy_base_locations_.empty()) {
			if (current_target_index_ >= enemy_base_locations_.size()) {
				current_target_index_ = 0;
			}
			Point2D target_location = enemy_base_locations_[current_target_index_];
			for (const auto &unit : combat_units) { // Command units to attack the target location
				if (unit->orders.empty()) {
					Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, target_location);
				}
			}

			current_target_index_++;
		}
	}
}

void BasicSc2Bot::AssignWorkersToExtractors() {
	Units extractors = GetUnitsOfType(UNIT_TYPEID::ZERG_EXTRACTOR);
	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);

	for (const Unit *extractor : extractors) {
		int required_workers = extractor->ideal_harvesters - extractor->assigned_harvesters;
		if (required_workers > 0) {
			for (int i = 0; i < required_workers && !drones.empty(); ++i) {
				const Unit *drone = drones.back();
				Actions()->UnitCommand(drone, ABILITY_ID::SMART, extractor);
				drones.pop_back();
			}
		}
	}
}

bool BasicSc2Bot::TryBuildVespeneExtractor() {
	const int max_extractors = 5;
	int current_extractors = GetUnitsOfType(UNIT_TYPEID::ZERG_EXTRACTOR).size();
	if (current_extractors >= max_extractors) { // If max extractor count hit, dont build
		return false;
	}

	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);
	if (drones.empty())
		return false;

	const Unit *drone = nullptr;
	for (const auto &d : drones) { // Find idle drone
		if (d->orders.empty()) {
			drone = d;
			break;
		}
	}
	if (!drone)
		return false;

	const Unit *vespene_geyser = FindNearestVespenseGeyser(drone->pos); // Find nearest vespene geyser
	if (!vespene_geyser)
		return false;

	Units extractors = GetUnitsOfType(UNIT_TYPEID::ZERG_EXTRACTOR);
	for (const auto &extractor : extractors) {
		if (DistanceSquared2D(extractor->pos, vespene_geyser->pos) < 1.0f) {
			return false;
		}
	}

	Actions()->UnitCommand(drone, ABILITY_ID::BUILD_EXTRACTOR, vespene_geyser); // Set drone to build extractor
	return true;
}

bool BasicSc2Bot::QueenInjectLarvae() {
	Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);

	hatcheries.insert(hatcheries.end(), lairs.begin(), lairs.end());
	hatcheries.insert(hatcheries.end(), hives.begin(), hives.end());

	for (const Unit *base : hatcheries) { // Skip incomplete bases
		if (base->build_progress < 1.0f) {
			continue;
		}
		if (!HasQueenAssigned(base)) { // If base has no queen, make queen
			if (Observation()->GetMinerals() >= 150) {
				Actions()->UnitCommand(base, ABILITY_ID::TRAIN_QUEEN);
				return true;
			}
		}

		Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);
		for (const Unit *queen : queens) {
			if (queen->energy >= 25 && DistanceSquared2D(queen->pos, base->pos) < 10 * 10) {
				Actions()->UnitCommand(queen, ABILITY_ID::EFFECT_INJECTLARVA, base);
				return true;
			}
		}
	}

	return false;
}

bool BasicSc2Bot::HasQueenAssigned(const Unit *base) {
	Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);

	for (const Unit *queen : queens) {
		if (DistanceSquared2D(base->pos, queen->pos) < 10 * 10) {
			return true;
		}
	}
	for (const auto &order : base->orders) {
		if (order.ability_id == ABILITY_ID::TRAIN_QUEEN) { // Queen being trained at this base
			return true;
		}
	}

	return false;
}

bool BasicSc2Bot::TryTrainOverlord() {
	const ObservationInterface *observation = Observation();

	if (observation->GetFoodCap() >= 200) { // Stop tarining overlords if food cap reached
		// std::cout << "supply cap reached, stop training \n";				// For Debugging
		return false;
	}

	if (observation->GetFoodUsed() >= observation->GetFoodCap() - 2) { // check if overlord needed
		Units overlords = GetUnitsOfType(UNIT_TYPEID::ZERG_OVERLORD);
		for (const auto &overlord : overlords) { // if overlord building, dont train
			if (overlord->build_progress < 1.0f) {
				// std::cout << "overlord already being made \n";				// For Debugging
				return false;
			}
		}
		Units larvae = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);
		if (!larvae.empty() && observation->GetMinerals() >= 100) {
			Actions()->UnitCommand(larvae.front(), ABILITY_ID::TRAIN_OVERLORD);
			// std::cout << "training new overlord \n";							// For Debugging
			return true;
		}
	}
	return false;
}

const Unit *BasicSc2Bot::FindNearestMineralPatch(const Point2D &start) {
	Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
	float closest_distance = std::numeric_limits<float>::max();
	const Unit *target = nullptr;
	for (const auto &u : units) {
		if ((u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD || u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD750 || u->unit_type == UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD ||
		     u->unit_type == UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD750) &&
		    u->mineral_contents > 0) {
			float distance = DistanceSquared2D(u->pos, start);
			if (distance < closest_distance) {
				closest_distance = distance;
				target = u;
			}
		}
	}
	return target;
}

const Unit *BasicSc2Bot::FindNearestVespenseGeyser(const Point2D &start) {
	Units geysers = Observation()->GetUnits(Unit::Alliance::Neutral, [](const Unit &unit) {
		return unit.unit_type == UNIT_TYPEID::NEUTRAL_VESPENEGEYSER || unit.unit_type == UNIT_TYPEID::NEUTRAL_PROTOSSVESPENEGEYSER ||
		       unit.unit_type == UNIT_TYPEID::NEUTRAL_SPACEPLATFORMGEYSER;
	});

	float closest_distance = std::numeric_limits<float>::max();
	const Unit *target = nullptr;

	for (const auto &geyser : geysers) {
		bool geyser_occupied = false; // Check for if vespene gyser is taken

		Units units_on_geyser = Observation()->GetUnits(
		    Unit::Alliance::Self, [geyser](const Unit &unit) { return unit.unit_type == UNIT_TYPEID::ZERG_EXTRACTOR && DistanceSquared2D(unit.pos, geyser->pos) < 1.0f; });

		Units enemy_units_on_geyser = Observation()->GetUnits(
		    Unit::Alliance::Enemy, [geyser](const Unit &unit) { return unit.unit_type == UNIT_TYPEID::ZERG_EXTRACTOR && DistanceSquared2D(unit.pos, geyser->pos) < 1.0f; });

		if (!units_on_geyser.empty() || !enemy_units_on_geyser.empty()) {
			geyser_occupied = true;
		}

		if (!geyser_occupied) {
			float distance = DistanceSquared2D(geyser->pos, start);
			if (distance < closest_distance) {
				closest_distance = distance;
				target = geyser;
			}
		}
	}
	return target;
}

Units BasicSc2Bot::GetUnitsOfType(UNIT_TYPEID type) {
	Units units = Observation()->GetUnits(Unit::Alliance::Self);
	Units units_vector;

	for (const auto &unit : units) {
		if (unit->unit_type == type) {    // If unit of the same type
			units_vector.push_back(unit); // Push to the vector
		}
	}

	return units_vector; // Return the vector
}

bool BasicSc2Bot::TryExpand(AbilityID build_ability, UnitTypeID worker_type) {
	const ObservationInterface *observation = Observation();
	std::vector<std::pair<float, Point3D>> distances;

	if (expansions_.empty()) {
		return false;
	}

	for (const auto &expansion : expansions_) { // Calculate distances for all expansions
		float current_distance = Distance2D(startLocation_, expansion);
		if (current_distance > 1.0f) { // Skip current base location
			distances.push_back({current_distance, expansion});
		}
	}

	std::sort(distances.begin(), distances.end(), [](const std::pair<float, Point3D> &a, const std::pair<float, Point3D> &b) { return a.first < b.first; }); // Sort by distance

	for (size_t i = 0; i < distances.size(); ++i) {
		const Point3D &expansion = distances[i].second;
		bool already_has_base = false;
		Units bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
		bases.insert(bases.end(), GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR).begin(), GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR).end());
		bases.insert(bases.end(), GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE).begin(), GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE).end());

		for (const auto &base : bases) { // Check if we already have a base at this location
			if (DistanceSquared2D(base->pos, expansion) < 4.0f) {
				already_has_base = true;
				break;
			}
		}

		if (already_has_base) { // Skip if we already have a base here
			continue;
		}

		if (Query()->Placement(build_ability, expansion)) {
			if (TryBuildStructure(build_ability, worker_type, expansion, true)) {
				return true;
			}
		}
	}

	return false;
}

bool BasicSc2Bot::TryUpgradeBase() {
	Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);

	for (const Unit *hatchery : hatcheries) {
		if (hatchery->build_progress < 1.0f) { // Skip if hatchery incomplete
			continue;
		}
		if (Observation()->GetMinerals() >= 150 && Observation()->GetVespene() >= 100) {
			if (!GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) {
				Actions()->UnitCommand(hatchery, ABILITY_ID::MORPH_LAIR);
				return true;
			}
		}
	}

	for (const Unit *lair : lairs) {
		if (lair->build_progress < 1.0f) { // Skip lair incomplete
			continue;
		}
		if (Observation()->GetMinerals() >= 200 && Observation()->GetVespene() >= 150) {
			if (!GetUnitsOfType(UNIT_TYPEID::ZERG_INFESTATIONPIT).empty()) {
				Actions()->UnitCommand(lair, ABILITY_ID::MORPH_HIVE);
				return true;
			}
		}
	}

	return false;
}

bool BasicSc2Bot::TryBuildStructure(AbilityID build_ability, UnitTypeID worker_type, const Point3D &location, bool check_placement) {
	const ObservationInterface *observation = Observation();

	// Check resources before proceeding
	if (observation->GetMinerals() < 300) {
		return false;
	}

	// Get available workers
	Units workers = observation->GetUnits(
	    Unit::Self, [worker_type](const Unit &unit) { return unit.unit_type == worker_type && (unit.orders.empty() || unit.orders[0].ability_id == ABILITY_ID::HARVEST_GATHER); });

	if (workers.empty()) {
		return false;
	}

	// Use the first available worker
	const Unit *worker = workers.front();

	// Stop the worker and issue the build command
	Actions()->UnitCommand(worker, ABILITY_ID::STOP);
	Actions()->UnitCommand(worker, build_ability, location);
	return true;
}