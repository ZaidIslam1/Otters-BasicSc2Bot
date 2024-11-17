#include "BasicSc2Bot.h"
#include <sc2api/sc2_api.h>
#include <iostream>

using namespace sc2;
// Windows
// ./BasicSc2Bot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map
// Mac
// ./BasicSc2Bot -c -a zerg -d Hard -m CactusValleyLE.SC2Map

void BasicSc2Bot::OnGameStart() {
}
void BasicSc2Bot::OnStep() {
	std::cout << "Minerals: " << Observation()->GetMinerals() << " Vespene Gas: " << Observation()->GetVespene() << std::endl;
	TryBuildHatchery();
}

// In your bot class.
void BasicSc2Bot::OnUnitIdle(const Unit* unit) {
	
	switch (unit->unit_type.ToType()) {
		case UNIT_TYPEID::ZERG_HIVE: {
			Actions()->UnitCommand(unit, ABILITY_ID::TRAIN_DRONE);
			std::cout << "Trained Drone\n";
			break;
		}
		case UNIT_TYPEID::ZERG_DRONE: {
			const Unit* mineral_target = FindNearestMineralPatch(unit->pos);
			if (!mineral_target) {
				break;
			}
			Actions()->UnitCommand(unit, ABILITY_ID::SMART, mineral_target);
			break;
		}
		default: {
			break;
		}
	}
}

const Unit* BasicSc2Bot::FindNearestMineralPatch(const Point2D& start) {
	Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
	float distance = std::numeric_limits<float>::max();
	const Unit* target = nullptr;
	for (const auto& u : units) {
		if (u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD) {
			float d = DistanceSquared2D(u->pos, start);
			if (d < distance) {
				distance = d;
				target = u;
			}
		}
	}
	return target;
}

const Unit* BasicSc2Bot::FindNearestVespeneGeyser(const Point2D& start) {
	Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
	float distance = std::numeric_limits<float>::max();
	const Unit* target = nullptr;
	for (const auto& u : units) {
		if (u->unit_type == UNIT_TYPEID::NEUTRAL_VESPENEGEYSER) {
			float d = DistanceSquared2D(u->pos, start);
			if (d < distance) {
				distance = d;
				target = u;
			}
		}
	}
	return target;
}


bool BasicSc2Bot::TryBuildStructure(ABILITY_ID ability_type_for_structure, UNIT_TYPEID unit_type = UNIT_TYPEID::ZERG_DRONE) {
	const ObservationInterface* observation = Observation();

	// If a unit already is building a supply structure of this type, do nothing.
	// Also get an scv to build the structure.
	const Unit* unit_to_build = nullptr;
	Units units = observation->GetUnits(Unit::Alliance::Self);
	for (const auto& unit : units) {
		for (const auto& order : unit->orders) {
			if (order.ability_id == ability_type_for_structure) {
				return false;
			}
		}

		if (unit->unit_type == unit_type) {
			unit_to_build = unit;
		}
	}

	float rx = GetRandomScalar();
	float ry = GetRandomScalar();

	Actions()->UnitCommand(unit_to_build, ability_type_for_structure, Point2D(unit_to_build->pos.x + rx * 15.0f, unit_to_build->pos.y + ry * 15.0f));
	std::cout << "Built structure\n";

	return true;
}

bool BasicSc2Bot::TryBuildHatchery() {
	const ObservationInterface* observation = Observation();

	// If we are not supply capped, don't build a supply depot.
	if (observation->GetFoodUsed() <= observation->GetFoodCap() - 2)
		return false;

	// Try and build a depot. Find a random SCV and give it the order.
	return TryBuildStructure(ABILITY_ID::BUILD_HATCHERY);
}

bool BasicSc2Bot::TryBuildExtractor() {
	const ObservationInterface* observation = Observation();

	// If we are not supply capped, don't build a supply depot.
	if (observation->GetFoodUsed() <= observation->GetFoodCap() - 2)
		return false;
	
	Units units = observation->GetUnits(Unit::Alliance::Self);
	for (auto& i : units) {
		if (i->unit_type == UNIT_TYPEID::ZERG_EXTRACTOR) {
			return false;
		}
	}

	// Try and build a depot. Find a random SCV and give it the order.
	return TryBuildStructure(ABILITY_ID::BUILD_EXTRACTOR);
}