#include "entity_data_extraction.h"
#include "log.h"
#include "util/algorithms.h"
#include "util/on_scope_exit.h"
#include "../../res/resource_playlunky64.h"

#include <charconv>

#include <nlohmann/json.hpp>
#include <Windows.h>

using namespace nlohmann;

struct AnimationData {
	std::int32_t FirstTileIndex;
	std::int32_t NumTiles;
};
struct EntityData {
	std::uint16_t Id;
	std::map<std::uint8_t, AnimationData> Animations;
	std::int32_t TextureId;
	std::int32_t TileX{ -1 };
	std::int32_t TileY{ -1 };
};
struct TextureData {
	std::string Path;
	std::uint32_t Width;
	std::uint32_t Height;
	std::uint32_t NumTilesWidth;
	std::uint32_t NumTilesHeight;
	std::uint32_t TileWidth;
	std::uint32_t TileHeight;
	std::uint32_t OffsetWidth;
	std::uint32_t OffsetHeight;
};

void from_json(const nlohmann::json& j, AnimationData& anim) {
	anim.FirstTileIndex = j["texture"];
	anim.NumTiles = j["count"];
}
void from_json(const nlohmann::json& j, EntityData& ent) {
	ent.Id = j["id"];
	std::unordered_map<std::string, AnimationData> animations;
	j["animations"].get_to(animations);
	for (auto& [id_str, animation_data] : animations) {
		std::uint8_t id;
		std::from_chars(id_str.c_str(), id_str.c_str() + id_str.size(), id);
		ent.Animations[id] = animation_data;
	}
	ent.TextureId = j["texture"];
	if (j.contains("tile_x")) {
		ent.TileX = j["tile_x"];
		ent.TileY = j["tile_y"];
	}
}
void from_json(const nlohmann::json& j, TextureData& tex) {
	tex.Path = j["path"];
	tex.Width = j["width"];
	tex.Height = j["height"];
	tex.NumTilesWidth = j["num_tiles"]["width"];
	tex.NumTilesHeight = j["num_tiles"]["height"];
	tex.TileWidth = j["tile_width"];
	tex.TileHeight = j["tile_height"];
	tex.OffsetWidth = j["offset"]["width"];
	tex.OffsetHeight = j["offset"]["height"];
}

void EntityDataExtractor::PreloadEntityMappings() {
	const auto [entities, textures] = []() {
		std::unordered_map<std::string, EntityData> entities;
		std::unordered_map<std::int32_t, TextureData> textures;

		{
			auto acquire_json_resource = [](LPSTR resource) {
				HMODULE this_module = GetModuleHandle("playlunky64.dll");
				if (HRSRC entities_json_resource = FindResource(this_module, resource, MAKEINTRESOURCE(JSON_FILE))) {
					if (HGLOBAL entities_json_data = LoadResource(this_module, entities_json_resource)) {
						DWORD entities_json_size = SizeofResource(this_module, entities_json_resource);
						return std::pair{ entities_json_data, std::string_view{ (const char*)LockResource(entities_json_data), entities_json_size } };
					}
				}
				return std::pair{ HGLOBAL{ NULL }, std::string_view{} };
			};
			auto [entities_res, entities_json] = acquire_json_resource(MAKEINTRESOURCE(ENTITIES_JSON));
			auto [textures_res, textures_json] = acquire_json_resource(MAKEINTRESOURCE(TEXTURES_JSON));
			OnScopeExit release_resources{ [entities_res, textures_res] {
				UnlockResource(entities_res);
				UnlockResource(textures_res);
			} };

			json::parse(entities_json).get_to(entities);
			{
				std::unordered_map<std::string, TextureData> string_mapped_textures;
				json::parse(textures_json).get_to(string_mapped_textures);
				for (auto& [id_str, texture_data] : string_mapped_textures) {
					std::int32_t id;
					std::from_chars(id_str.c_str(), id_str.c_str() + id_str.size(), id);
					textures[id] = std::move(texture_data);
				}
			}
		}

		return std::pair(std::move(entities), std::move(textures));
	}();

	struct EntityMappingInfo {
		std::string_view EntityPath;
		std::vector<std::string_view> EntityNames;
		std::uint32_t AdditionalHeight;
		std::uint32_t InitialHeight;
	};
	std::array entity_mapping_info{
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/alien_queen.png", { "ENT_TYPE_MONS_ALIENQUEEN", "ENT_TYPE_FX_ALIENQUEEN_EYE", "ENT_TYPE_FX_ALIENQUEEN_EYEBALL" }, 320 + 160 },
		
		EntityMappingInfo{ "Data/Textures/Entities/Pets/monty.png", { "ENT_TYPE_MONS_PET_DOG" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Pets/percy.png", { "ENT_TYPE_MONS_PET_CAT" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Pets/poochi.png", { "ENT_TYPE_MONS_PET_HAMSTER" }, 160 },

		EntityMappingInfo{ "Data/Textures/Entities/Mounts/turkey.png", { "ENT_TYPE_MOUNT_TURKEY" }, 160 + 160 + 128, 128 },
		EntityMappingInfo{ "Data/Textures/Entities/Mounts/rockdog.png", { "ENT_TYPE_MOUNT_ROCKDOG" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Mounts/axolotl.png", { "ENT_TYPE_MOUNT_AXOLOTL", "ENT_TYPE_FX_AXOLOTL_HEAD_ENTERING_DOOR" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Mounts/qilin.png", { "ENT_TYPE_MOUNT_QILIN" }, 160 },

		EntityMappingInfo{ "Data/Textures/Entities/People/shopkeeper.png", { "ENT_TYPE_MONS_SHOPKEEPER" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/bodyguard.png", { "ENT_TYPE_MONS_BODYGUARD" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/People/hunduns_servant.png", { "ENT_TYPE_MONS_HUNDUNS_SERVANT" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/People/old_hunter.png", { "ENT_TYPE_MONS_OLD_HUNTER" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/merchant.png", { "ENT_TYPE_MONS_MERCHANT" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/thief.png", { "ENT_TYPE_MONS_THIEF" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/parmesan.png", { "ENT_TYPE_MONS_SISTER_PARMESAN" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/parsley.png", { "ENT_TYPE_MONS_SISTER_PARSLEY" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/parsnip.png", { "ENT_TYPE_MONS_SISTER_PARSNIP" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/People/yang.png", { "ENT_TYPE_MONS_YANG" }, 160 + 80 },

		EntityMappingInfo{ "Data/Textures/Entities/Monsters/snake.png", { "ENT_TYPE_MONS_SNAKE" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/bat.png", { "ENT_TYPE_MONS_BAT" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/fly.png", { "ENT_TYPE_ITEM_FLY" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/skeleton.png", { "ENT_TYPE_MONS_SKELETON" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/spider.png", { "ENT_TYPE_MONS_SPIDER" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/ufo.png", { "ENT_TYPE_MONS_UFO" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/alien.png", { "ENT_TYPE_MONS_ALIEN" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/cobra.png", { "ENT_TYPE_MONS_COBRA" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/scorpion.png", { "ENT_TYPE_MONS_SCORPION" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/golden_monkey.png", { "ENT_TYPE_MONS_GOLDMONKEY" }, 160, 128 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/bee.png", { "ENT_TYPE_MONS_BEE" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/magmar.png", { "ENT_TYPE_MONS_MAGMAMAN" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/vampire.png", { "ENT_TYPE_MONS_VAMPIRE" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/vlad.png", { "ENT_TYPE_MONS_VLAD" }, 160 + 80 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/leprechaun.png", { "ENT_TYPE_MONS_LEPRECHAUN" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/cave_man.png", { "ENT_TYPE_MONS_CAVEMAN" }, 160 + 80, 256 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/robot.png", { "ENT_TYPE_MONS_ROBOT" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/imp.png", { "ENT_TYPE_MONS_IMP" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/man_trap.png", { "ENT_TYPE_MONS_MANTRAP" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/tiki_man.png", { "ENT_TYPE_MONS_TIKIMAN" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/fire_bug.png", { "ENT_TYPE_MONS_FIREBUG", "ENT_TYPE_MONS_FIREBUG_UNCHAINED" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/mole.png", { "ENT_TYPE_MONS_MOLE" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/witch_doctor.png", { "ENT_TYPE_MONS_WITCHDOCTOR" }, 160, 128 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/witch_doctor_skull.png", { "ENT_TYPE_MONS_WITCHDOCTORSKULL" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/horned_lizard.png", { "ENT_TYPE_MONS_HORNEDLIZARD" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/monkey.png", { "ENT_TYPE_MONS_MONKEY" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/hang_spider.png", { "ENT_TYPE_MONS_HANGSPIDER" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/mosquito.png", { "ENT_TYPE_MONS_MOSQUITO" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/jiangshi.png", { "ENT_TYPE_MONS_JIANGSHI" }, 160, 128 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/hermit_crab.png", { "ENT_TYPE_MONS_HERMITCRAB" }, 160, 256 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/flying_fish.png", { "ENT_TYPE_MONS_FISH" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/octopus.png", { "ENT_TYPE_MONS_OCTOPUS" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/female_jiangshi.png", { "ENT_TYPE_MONS_FEMALE_JIANGSHI" }, 160, 128 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/croc_man.png", { "ENT_TYPE_MONS_CROCMAN" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/sorceress.png", { "ENT_TYPE_MONS_SORCERESS" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/cat_mummy.png", { "ENT_TYPE_MONS_CATMUMMY" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/necromancer.png", { "ENT_TYPE_MONS_NECROMANCER" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/yeti.png", { "ENT_TYPE_MONS_YETI" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/proto_shopkeeper.png", { "ENT_TYPE_MONS_PROTOSHOPKEEPER" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/jumpdog.png", { "ENT_TYPE_MONS_JUMPDOG" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/tadpole.png", { "ENT_TYPE_MONS_TADPOLE" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/olmite_naked.png", { "ENT_TYPE_MONS_OLMITE_NAKED" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/grub.png", { "ENT_TYPE_MONS_GRUB", "ENT_TYPE_ITEM_EGGSAC" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/frog.png", { "ENT_TYPE_MONS_FROG" }, 160 },
		EntityMappingInfo{ "Data/Textures/Entities/Monsters/fire_frog.png", { "ENT_TYPE_MONS_FIREFROG" }, 160, 128 },

		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/quill_back.png", { "ENT_TYPE_MONS_CAVEMAN_BOSS" }, 320 + 160 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/giant_spider.png", { "ENT_TYPE_MONS_GIANTSPIDER" }, 320, 256 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/queen_bee.png", { "ENT_TYPE_MONS_QUEENBEE" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/mummy.png", { "ENT_TYPE_MONS_MUMMY" }, 320 },
		//EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/anubis.png", { "ENT_TYPE_MONS_ANUBIS" }, 320 + 160 },
		//EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/anubis2.png", { "ENT_TYPE_MONS_ANUBIS2" }, 320 + 160 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/lamassu.png", { "ENT_TYPE_MONS_LAMASSU" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/yeti_king.png", { "ENT_TYPE_MONS_YETIKING" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/yeti_queen.png", { "ENT_TYPE_MONS_YETIQUEEN" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/crab_man.png", { "ENT_TYPE_MONS_CRABMAN" }, 320, 256 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/lavamander.png", { "ENT_TYPE_MONS_LAVAMANDER" }, 320, 256 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/giant_fly.png", { "ENT_TYPE_MONS_GIANTFLY", "ENT_TYPE_ITEM_GIANTFLY_HEAD" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/giant_clam.png", { "ENT_TYPE_ITEM_GIANTCLAM_TOP", "ENT_TYPE_ACTIVEFLOOR_GIANTCLAM_BASE" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/ammit.png", { "ENT_TYPE_MONS_AMMIT" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/madame_tusk.png", { "ENT_TYPE_MONS_MADAMETUSK" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/eggplant_minister.png", { "ENT_TYPE_MONS_EGGPLANT_MINISTER" }, 320, 256 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/giant_frog.png", { "ENT_TYPE_MONS_GIANTFROG" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/giant_fish.png", { "ENT_TYPE_MONS_GIANTFISH" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/waddler.png", { "ENT_TYPE_MONS_STORAGEGUY" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/osiris.png", { "ENT_TYPE_MONS_OSIRIS_HEAD", "ENT_TYPE_MONS_OSIRIS_HAND" }, 320 + 160 },
		EntityMappingInfo{ "Data/Textures/Entities/BigMonsters/alien_queen.png", { "ENT_TYPE_MONS_ALIENQUEEN", "ENT_TYPE_FX_ALIENQUEEN_EYE", "ENT_TYPE_FX_ALIENQUEEN_EYEBALL" }, 320 + 160 },

		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghist.png", { "ENT_TYPE_MONS_GHIST" }, 160, 384 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost.png", { "ENT_TYPE_MONS_GHOST" }, 320 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost_sad.png", { "ENT_TYPE_MONS_GHOST_MEDIUM_SAD" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost_happy.png", { "ENT_TYPE_MONS_GHOST_MEDIUM_HAPPY" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost_small_sad.png", { "ENT_TYPE_MONS_GHOST_SMALL_SAD" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost_small_happy.png", { "ENT_TYPE_MONS_GHOST_SMALL_HAPPY" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost_small_surprised.png", { "ENT_TYPE_MONS_GHOST_SMALL_SURPRISED" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Ghost/ghost_small_angry.png", { "ENT_TYPE_MONS_GHOST_SMALL_ANGRY" }, 0 },

		EntityMappingInfo{ "Data/Textures/Entities/Critters/snail.png", { "ENT_TYPE_MONS_CRITTERSNAIL" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/dung_beetle.png", { "ENT_TYPE_MONS_CRITTERDUNGBEETLE" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/butterfly.png", { "ENT_TYPE_MONS_CRITTERBUTTERFLY" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/crab.png", { "ENT_TYPE_MONS_CRITTERCRAB" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/fish.png", { "ENT_TYPE_MONS_CRITTERFISH" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/anchovy.png", { "ENT_TYPE_MONS_CRITTERANCHOVY" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/locust.png", { "ENT_TYPE_MONS_CRITTERLOCUST" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/firefly.png", { "ENT_TYPE_MONS_CRITTERFIREFLY" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/penguin.png", { "ENT_TYPE_MONS_CRITTERPENGUIN" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/drone.png", { "ENT_TYPE_MONS_CRITTERDRONE" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/slime.png", { "ENT_TYPE_MONS_CRITTERSLIME" }, 0 },
		EntityMappingInfo{ "Data/Textures/Entities/Critters/birdies.png", { "ENT_TYPE_FX_BIRDIES" }, 0 },
	};

	for (const EntityMappingInfo& mapping_info : entity_mapping_info) {
		SheetSize size{ .Width{ 0 }, .Height{ mapping_info.InitialHeight } };
		std::vector<TileMapping> tile_map{};

		for (const std::string_view entity_name : mapping_info.EntityNames) {
			if (entities.contains(std::string{ entity_name })) {
				const EntityData& entity_data = entities.at(std::string{ entity_name });
				if (textures.contains(entity_data.TextureId)) {
					const TextureData& texture_data = textures.at(entity_data.TextureId);

					const std::uint32_t tile_width = texture_data.TileWidth;
					const std::uint32_t tile_height = texture_data.TileHeight;
					const std::uint32_t num_tiles_width = texture_data.NumTilesWidth;
					const std::uint32_t num_tiles_height = texture_data.NumTilesHeight;
					const std::uint32_t offset_width = texture_data.OffsetWidth;
					const std::uint32_t offset_height = texture_data.OffsetHeight;

					if (entity_data.Animations.empty()) {
						const std::int32_t tile_x = entity_data.TileX;
						const std::int32_t tile_y = entity_data.TileY;
						const std::int32_t tile_index = tile_y * num_tiles_width + tile_x;
						const std::int32_t real_tile_x = tile_index % num_tiles_width;
						const std::int32_t real_tile_y = tile_index / num_tiles_width;

						tile_map.push_back(TileMapping{
								.SourceTile{
									0,
									size.Height,
									tile_width,
									tile_height + size.Height
								},
								.TargetTile{
									offset_width + real_tile_x * tile_width,
									offset_height + real_tile_y * tile_height,
									offset_width + (real_tile_x + 1) * tile_width,
									offset_height + (real_tile_y + 1) * tile_height
								}
							});
						
						size.Width = std::max(size.Width, tile_width);
						size.Height += tile_height;
					}
					else {
						std::vector<std::int32_t> unique_tile_indices;
						for (const auto& [animation_id, animation_data] : entity_data.Animations) {
							const std::int32_t first_tile = animation_data.FirstTileIndex;
							for (std::int32_t i = 0; i < animation_data.NumTiles; i++) {
								const std::int32_t tile_index = first_tile + i;
								if (!algo::contains(unique_tile_indices, tile_index)) {
									unique_tile_indices.push_back(tile_index);
								}
							}
						}

						const std::uint32_t source_num_tiles_width
							= static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(unique_tile_indices.size()))));
						const std::uint32_t source_num_tiles_height
							= static_cast<std::uint32_t>(std::ceil(static_cast<float>(unique_tile_indices.size()) / source_num_tiles_width));

						std::int32_t source_tile_index = 0;
						for (const std::int32_t tile_index : unique_tile_indices) {
							const std::int32_t source_tile_x = source_tile_index % source_num_tiles_width;
							const std::int32_t source_tile_y = source_tile_index / source_num_tiles_width;
							source_tile_index++;

							const std::int32_t target_tile_x = tile_index % num_tiles_width;
							const std::int32_t target_tile_y = tile_index / num_tiles_width;

							tile_map.push_back(TileMapping{
									.SourceTile{
										source_tile_x * tile_width,
										source_tile_y * tile_height + size.Height,
										(source_tile_x + 1) * tile_width,
										(source_tile_y + 1) * tile_height + size.Height,
									},
									.TargetTile{
										offset_width + target_tile_x * tile_width,
										offset_height + target_tile_y * tile_height,
										offset_width + (target_tile_x + 1) * tile_width,
										offset_height + (target_tile_y + 1) * tile_height,
									}
								});
						}

						size.Width = std::max(size.Width, source_num_tiles_width * tile_width);
						size.Height += source_num_tiles_height * tile_height;
					}
				}
				else {
					LogError("Can't find texture {} for entity {}...", entity_data.TextureId, entity_name);
				}
			}
			else {
				LogError("Can't find entity {}...", entity_name);
			}
		}

		size.Height += mapping_info.AdditionalHeight;

		m_EntityMapping.push_back(EntityMapping{
				.EntityPath{ mapping_info.EntityPath },
				.SourceSheet{
					.Path{ mapping_info.EntityPath },
					.Size{ size },
					.TileMap{ std::move(tile_map) }
				},
				.SourceHeight{ size.Height - mapping_info.AdditionalHeight }
			});
	}
}

std::optional<SourceSheet> EntityDataExtractor::GetEntitySourceSheet(std::string_view entity_sheet) const {
	if (const EntityMapping* mapping = algo::find(m_EntityMapping, &EntityMapping::EntityPath, entity_sheet)) {
		return mapping->SourceSheet;
	}
	LogError("Could not find data for sheet {}", entity_sheet);
	return std::nullopt;
}
std::optional<SourceSheet> EntityDataExtractor::GetAdditionalMapping(std::string_view entity_sheet, Tile relative_source_tile, Tile target_tile) const {
	if (const EntityMapping* mapping = algo::find(m_EntityMapping, &EntityMapping::EntityPath, entity_sheet)) {
		relative_source_tile.Top += mapping->SourceHeight;
		relative_source_tile.Bottom += mapping->SourceHeight;

		return SourceSheet{
			.Path{ entity_sheet },
			.Size{ mapping->SourceSheet.Size },
			.TileMap = std::vector<TileMapping>{
				TileMapping{
					.SourceTile{ relative_source_tile },
					.TargetTile{ target_tile },
				}
			}
		};
	}
	LogError("Could not find data for sheet {}", entity_sheet);
	return std::nullopt;
}