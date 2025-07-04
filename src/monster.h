#pragma once

#include <bitset>
#include <climits>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "character_id.h"
#include "color.h"
#include "creature.h"
#include "cursesdef.h"
#include "damage.h"
#include "effect.h"
#include "enums.h"
#include "item.h"
#include "location_ptr.h"
#include "location_vector.h"
#include "pldata.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "visitable.h"

class Character;
class JsonIn;
class JsonObject;
class JsonOut;
class effect;
class item;
class player;
struct dealt_projectile_attack;
struct pathfinding_settings;
struct trap;

template<typename T>
class detached_ptr;


enum class mon_trigger;

class mon_special_attack
{
    public:
        int cooldown = 0;
        bool enabled = true;

        void serialize( JsonOut &json ) const;
        // deserialize inline in monster::load due to backwards/forwards compatibility concerns
};

enum monster_attitude : int {
    MATT_NULL = 0,
    MATT_FRIEND,
    MATT_FPASSIVE,
    MATT_FLEE,
    MATT_IGNORE,
    MATT_FOLLOW,
    MATT_ATTACK,
    MATT_ZLAVE,
    NUM_MONSTER_ATTITUDES
};

template<>
struct enum_traits<monster_attitude> {
    static constexpr monster_attitude last = monster_attitude::NUM_MONSTER_ATTITUDES;
};

enum monster_effect_cache_fields {
    MOVEMENT_IMPAIRED = 0,
    FLEEING,
    VISION_IMPAIRED,
    PATHFINDING_OVERRIDE,
    NUM_MEFF
};

enum monster_horde_attraction {
    MHA_NULL = 0,
    MHA_ALWAYS,
    MHA_LARGE,
    MHA_OUTDOORS,
    MHA_OUTDOORS_AND_LARGE,
    MHA_NEVER,
    NUM_MONSTER_HORDE_ATTRACTION
};

class monster : public Creature, public location_visitable<monster>
{
        friend class editmap;
        friend location_visitable<monster>;
    public:
        monster();
        monster( const mtype_id &id );
        monster( const mtype_id &id, const tripoint &pos );
        monster( const monster & );
        ~monster() override;
        monster &operator=( const monster & ) = delete;
        monster &operator=( monster && ) = delete;

        bool is_monster() const override {
            return true;
        }
        monster *as_monster() override {
            return this;
        }
        const monster *as_monster() const override {
            return this;
        }

        void poly( const mtype_id &id );
        bool can_upgrade() const;
        void hasten_upgrade();
        int get_upgrade_time() const;
        void allow_upgrade();
        void try_upgrade( bool pin_time );
        /// Check if monster is ready to reproduce and do so if possible, refreshing baby timer.
        void try_reproduce();
        /// Immediatly spawn an offspring without mutating baby timer.
        void reproduce();
        void refill_udders();
        void spawn( const tripoint &p );
        creature_size get_size() const override;
        units::mass get_weight() const override;
        units::mass weight_capacity() const override;
        units::volume get_volume() const;
        int get_hp( const bodypart_id & ) const override;
        int get_hp() const override;
        int get_hp_max( const bodypart_id & ) const override;
        int get_hp_max() const override;
        int hp_percentage() const override;

        float get_mountable_weight_ratio() const;

        // Access
        std::string get_name() const override;
        std::string name( unsigned int quantity = 1 ) const; // Returns the monster's formal name
        std::string name_with_armor() const; // Name, with whatever our armor is called
        // the creature-class versions of the above
        std::string disp_name( bool possessive = false, bool capitalize_first = false ) const override;
        std::string skin_name() const override;
        void get_HP_Bar( nc_color &color, std::string &text ) const;
        std::pair<std::string, nc_color> get_attitude() const;
        int print_info( const catacurses::window &w, int vStart, int vLines, int column ) const override;

        nc_color basic_symbol_color() const override;
        nc_color symbol_color() const override;
        const std::string &symbol() const override;
        bool is_symbol_highlighted() const override;

        nc_color color_with_effects() const; // Color with fire, beartrapped, etc.

        std::string extended_description() const override;
        // Inverts color if inv==true
        bool has_flag( m_flag f ) const override; // Returns true if f is set (see mtype.h)
        bool can_see() const;      // MF_SEES and no MF_BLIND
        bool can_hear() const;     // MF_HEARS and no MF_DEAF
        bool can_submerge() const; // MF_AQUATIC or swims() or MF_NO_BREATH, and not MF_ELECTRONIC
        bool can_drown() const;    // MF_AQUATIC or swims() or MF_NO_BREATHE or flies()
        bool can_climb() const;         // climbs() or flies()
        bool digging() const override;  // digs() or can_dig() and diggable terrain
        bool can_dig() const;
        bool digs() const;
        bool flies() const;
        bool climbs() const;
        bool swims() const;
        // Returns false if the monster is stunned, has 0 moves or otherwise wouldn't act this turn
        bool can_act() const;
        int sight_range( int light_level ) const override;
        bool made_of( const material_id &m ) const override; // Returns true if it's made of m
        bool made_of_any( const std::set<material_id> &ms ) const override;
        bool made_of( phase_id p ) const; // Returns true if its phase is p

        bool avoid_trap( const tripoint &pos, const trap &tr ) const override;

        void serialize( JsonOut &json ) const;
        void deserialize( JsonIn &jsin );

        tripoint move_target(); // Returns point at the end of the monster's current plans
        Creature *attack_target(); // Returns the creature at the end of plans (if hostile)

        // Movement
        void shift( point sm_shift ); // Shifts the monster to the appropriate submap
        void set_goal( const tripoint &p );
        // Updates current pos AND our plans
        bool is_wandering(); // Returns true if we have no plans

        /**
         * Checks whether we can move to/through p. This does not account for bashing.
         *
         * This is used in pathfinding and ONLY checks the terrain. It ignores players
         * and monsters, which might only block this tile temporarily.
         * will_move_to() checks for impassable terrain etc
         * can_reach_to() checks for z-level difference.
         * can_move_to() is a wrapper for both of them.
         * can_squeeze_to() checks for vehicle holes.
         */
        bool can_move_to( const tripoint &p ) const;
        bool can_reach_to( const tripoint &p ) const;
        bool will_move_to( const tripoint &p ) const;
        bool can_squeeze_to( const tripoint &p ) const;

        bool will_reach( point p ); // Do we have plans to get to (x, y)?
        int  turns_to_reach( point p ); // How long will it take?

        // Go in a straight line to p
        void set_dest( const tripoint &p );
        // Reset our plans, we've become aimless.
        void unset_dest();

        /**
         * Set p as wander destination.
         *
         * This will cause the monster to slowly move towards the destination,
         * unless there is an overriding smell or plan.
         *
         * @param p Destination of monster's wanderings
         * @param f The priority of the destination, as well as how long we should
         *          wander towards there.
         */
        void wander_to( const tripoint &p, int f ); // Try to get to (x, y), we don't know
        // the route.  Give up after f steps.

        // How good of a target is given creature (checks for visibility)
        float rate_target( Creature &c, float best, bool smart = false ) const;
        void plan();
        void move(); // Actual movement
        void footsteps( const tripoint &p ); // noise made by movement
        void shove_vehicle( const tripoint &remote_destination,
                            const tripoint &nearby_destination ); // shove vehicles out of the way

        // check if the given square could drown a drownable monster
        bool is_aquatic_danger( const tripoint &at_pos );

        // check if a monster at a position will drown and kill it if necessary
        // returns true if the monster dies
        // chance is the one_in( chance ) that the monster will drown
        bool die_if_drowning( const tripoint &at_pos, int chance = 1 );

        tripoint scent_move();
        int calc_movecost( const tripoint &f, const tripoint &t ) const;
        int calc_climb_cost( const tripoint &f, const tripoint &t ) const;

        bool is_immune_field( const field_type_id &fid ) const override;

        /**
         * Attempt to move to p.
         *
         * If there's something blocking the movement, such as infinite move
         * costs at the target, an existing NPC or monster, this function simply
         * aborts and does nothing.
         *
         * @param p Destination of movement
         * @param force If this is set to true, the movement will happen even if
         *              there's currently something, else than a creature, blocking the destination.
         * @param step_on_critter If this is set to true, the movement will happen even if
         *              there's currently a creature blocking the destination.
         *
         * @param stagger_adjustment is a multiplier for move cost to compensate for staggering.
         *
         * @return true if movement successful, false otherwise
         */
        bool move_to( const tripoint &p, bool force = false, bool step_on_critter = false,
                      float stagger_adjustment = 1.0 );

        /**
         * Attack any enemies at the given location.
         *
         * Attacks only if there is a creature at the given location towards
         * we are hostile.
         *
         * @return true if something was attacked, false otherwise
         */
        bool attack_at( const tripoint &p );

        /**
         * Try to smash/bash/destroy your way through the terrain at p.
         *
         * @return true if we destroyed something, false otherwise.
         */
        bool bash_at( const tripoint &p );

        /**
         * Try to push away whatever occupies p, then step in.
         * May recurse and try to make the creature at p push further.
         *
         * @param p Location of pushed object
         * @param boost A bonus on the roll to represent a horde pushing from behind
         * @param depth Number of recursions so far
         *
         * @return True if we managed to push something and took its place, false otherwise.
         */
        bool push_to( const tripoint &p, int boost, size_t depth );

        /** Returns innate monster bash skill, without calculating additional from helpers */
        int bash_skill();
        int bash_estimate();
        /** Returns ability of monster and any cooperative helpers to
         * bash the designated target.  **/
        int group_bash_skill( const tripoint &target );

        void stumble();
        void knock_back_to( const tripoint &to ) override;

        // Combat
        bool is_fleeing( Character &who ) const; // True if we're fleeing
        monster_attitude attitude( const Character *u = nullptr ) const; // See the enum above
        Attitude attitude_to( const Creature &other ) const override;
        void process_triggers(); // Process things that anger/scare us

        bool is_underwater() const override;
        bool is_on_ground() const override;
        bool is_warm() const override;
        bool in_species( const species_id &spec ) const override;

        bool has_weapon() const override;
        bool is_dead_state() const override; // check if we should be dead or not
        bool is_elec_immune() const override;
        bool is_immune_effect( const efftype_id & ) const override;
        bool is_immune_damage( damage_type ) const override;

        resistances resists() const;
        void absorb_hit( const bodypart_id &bp, damage_instance &dam ) override;
        bool block_hit( Creature *source, bodypart_id &bp_hit, damage_instance &d ) override;
        bool block_ranged_hit( Creature *source, bodypart_id &bp_hit, damage_instance &d ) override;
        void melee_attack( Creature &target );
        void melee_attack( Creature &target, float accuracy );
        void melee_attack( Creature &p, bool ) = delete;
        using Creature::deal_projectile_attack;
        void deal_projectile_attack( Creature *source, item *source_weapon,
                                     dealt_projectile_attack &attack ) override;
        void deal_projectile_attack( Creature *source, dealt_projectile_attack &attack ) override;
        void apply_damage( Creature *source, item *source_weapon, item *source_projectile, bodypart_id bp,
                           int dam,
                           bool bypass_med = false ) override;
        void apply_damage( Creature *source, item *source_weapon, bodypart_id bp, int dam,
                           bool bypass_med = false ) override;
        void apply_damage( Creature *source, bodypart_id bp, int dam,
                           bool bypass_med = false ) override;
        // create gibs/meat chunks/blood etc all over the place, does not kill, can be called on a dead monster.
        void explode();
        // Let the monster die and let its body explode into gibs
        void die_in_explosion( Creature *source );
        /**
         * Flat addition to the monsters @ref hp. If `overheal` is true, this is not capped by max hp.
         * Returns actually healed hp.
         */
        int heal( int delta_hp, bool overheal = false );
        /**
         * Directly set the current @ref hp of the monster (not capped at the maximal hp).
         * You might want to use @ref heal / @ref apply_damage or @ref deal_damage instead.
         */
        void set_hp( int hp );

        /** Processes monster-specific effects before calling Creature::process_effects(). */
        void process_effects_internal() override;

        /** Returns true if the monster has its movement impaired */
        bool movement_impaired();
        /** Processes effects which may prevent the monster from moving (bear traps, crushed, etc.).
         *  Returns false if movement is stopped. */
        bool move_effects( bool attacking ) override;
        /** Performs any monster-specific modifications to the arguments before passing to Creature::add_effect(). */
        void add_effect( const efftype_id &eff_id, const time_duration &dur, const bodypart_str_id &bp,
                         int intensity = 0, bool force = false, bool deferred = false ) override;
        void add_effect( const efftype_id &eff_id, const time_duration &dur );
        // Use the bodypart_str_id variant instead
        void add_effect( const efftype_id &eff_id, const time_duration &dur, body_part bp,
                         int intensity = 0, bool force = false, bool deferred = false ) = delete;
        /** Returns a std::string containing effects for descriptions */
        std::string get_effect_status() const;

        float power_rating() const override;
        float speed_rating() const override;

        int get_worn_armor_val( damage_type dt ) const;
        int  get_armor_cut( bodypart_id bp ) const override; // Natural armor, plus any worn armor
        int  get_armor_bash( bodypart_id bp ) const override; // Natural armor, plus any worn armor
        int  get_armor_bullet( bodypart_id bp ) const override; // Natural armor, plus any worn armor
        int  get_armor_type( damage_type dt, bodypart_id bp ) const override;

        float get_hit_base() const override;
        float get_dodge_base() const override;

        float  get_dodge() const override;       // Natural dodge, or 0 if we're occupied
        float  get_melee() const override;
        float  hit_roll() const;  // For the purposes of comparing to player::dodge_roll()
        float  dodge_roll() override;  // For the purposes of comparing to player::hit_roll()

        int get_grab_strength() const; // intensity of grabbed effect

        monster_horde_attraction get_horde_attraction();
        void set_horde_attraction( monster_horde_attraction mha );
        bool will_join_horde( int size );

        /** Returns multiplier on fall damage at low velocity (knockback/pit/1 z-level, not 5 z-levels) */
        float fall_damage_mod() const override;
        /** Deals falling/collision damage with terrain/creature at pos */
        int impact( int force, const tripoint &pos ) override;

        bool has_grab_break_tec() const override;

        float stability_roll() const override;
        void on_hit( Creature *source, bodypart_id bp_hit,
                     dealt_projectile_attack const *proj = nullptr ) override;
        void on_damage_of_type( int amt, damage_type dt, const bodypart_id &bp ) override;

        /** Resets a given special to its monster type cooldown value */
        void reset_special( const std::string &special_name );
        /** Resets a given special to a value between 0 and its monster type cooldown value. */
        void reset_special_rng( const std::string &special_name );
        /** Sets a given special to the given value */
        void set_special( const std::string &special_name, int time );
        /** Sets the enabled flag for the given special to false */
        void disable_special( const std::string &special_name );
        /** Return the lowest cooldown for an enabled special */
        int shortest_special_cooldown() const;

        void process_turn() override;
        /** Resets the value of all bonus fields to 0, clears special effect flags. */
        void reset_bonuses() override;
        /** Resets stats, and applies effects in an idempotent manner */
        void reset_stats() override;

        void die( Creature *killer ) override; //this is the die from Creature, it calls kill_mo
        void drop_items_on_death();

        // Other
        /**
         * Makes this monster into a fungus version
         * Returns false if no such monster exists
         * Returns true if monster is immune to spores, or if it has been fungalized
         */
        bool make_fungus();
        void make_friendly();
        /** Makes this monster an ally of the given monster. */
        void make_ally( const monster &z );
        // makes this monster a pet of the player
        void make_pet();
        // check if this monster is a pet of the player
        bool is_pet() const;
        // Add an item to inventory
        void add_item( detached_ptr<item> &&it );
        // check mech power levels and modify it.
        bool use_mech_power( int amt );
        bool check_mech_powered() const;
        int mech_str_addition() const;

        void process_items();

        const std::vector<item *> &get_items() const;
        detached_ptr<item> remove_item( item *it );
        location_vector<item>::iterator remove_item( location_vector<item>::iterator &it,
                detached_ptr<item> *result = nullptr );
        std::vector<detached_ptr<item>> clear_items();
        void drop_items();
        void drop_items( const tripoint &p );

        /**
         * Makes monster react to heard sound
         *
         * @param source Location of the sound source
         * @param vol Volume at the center of the sound source
         * @param distance Distance to sound source (currently just rl_dist)
         */
        void hear_sound( const tripoint &source, int vol, int distance );

        bool is_hallucination() const override;    // true if the monster isn't actually real

        field_type_id bloodType() const override;
        field_type_id gibType() const override;

        using Creature::add_msg_if_npc;
        void add_msg_if_npc( const std::string &msg ) const override;
        void add_msg_if_npc( const game_message_params &params, const std::string &msg ) const override;
        using Creature::add_msg_player_or_npc;
        void add_msg_player_or_npc( const std::string &player_msg,
                                    const std::string &npc_msg ) const override;
        void add_msg_player_or_npc( const game_message_params &params, const std::string &player_msg,
                                    const std::string &npc_msg ) const override;
        // TEMP VALUES
        tripoint wander_pos; // Wander destination - Just try to move in that direction
        int wandf;           // Urge to wander - Increased by sound, decrements each move


        Character *mounted_player = nullptr; // player that is mounting this creature
        character_id mounted_player_id; // id of player that is mounting this creature ( for save/load )
        character_id dragged_foe_id; // id of character being dragged by the monster
        units::mass get_carried_weight() const;
        units::volume get_carried_volume() const;

        // DEFINING VALUES
        int friendly;
        int anger = 0;
        int morale = 0;
        // Our faction (species, for most monsters)
        mfaction_id faction;
        // If we're related to a mission
        int mission_id;
        const mtype *type;
        // If true, don't spawn loot items as part of death.
        bool no_extra_death_drops;
        // If true, monster dies quietly and leaves no corpse.
        bool no_corpse_quiet = false;
        // Turned to false for simulating monsters during distant missions so they don't drop in sight.
        bool death_drops = true;
        bool is_dead() const;
        bool made_footstep;
        // If we're unique
        std::string unique_name;
        bool hallucination;
        // abstract for a fish monster representing a hidden stock of population in that area.
        int fish_population = 1;

        void setpos( const tripoint &p ) override;
        const tripoint &pos() const override;
        int posx() const override {
            return position.x;
        }
        int posy() const override {
            return position.y;
        }
        int posz() const override {
            return position.z;
        }

        short ignoring;

        bool aggro_character = true;

        std::optional<time_point> lastseen_turn;

        // Stair data.
        int staircount;

        // Ammunition if we use a gun.
        std::map<itype_id, int> ammo;

        /**
         * Convert this monster into an item (see @ref mtype::revert_to_itype).
         * Only useful for robots and the like, the monster must have at least
         * a non-empty item id as revert_to_itype.
         */
        detached_ptr<item> to_item() const;
        /**
         * Initialize values like speed / hp from data of an item.
         * This applies to robotic monsters that are spawned by invoking an item (e.g. turret),
         * and to reviving monsters that spawn from a corpse.
         */
        void init_from_item( const item &itm );

        time_point last_updated = calendar::turn_zero;
        /**
         * Do some cleanup and caching as monster is being unloaded from map.
         */
        void on_unload();
        /**
         * Retroactively update monster.
         * Call this after a preexisting monster has been placed on map.
         * Don't call for monsters that have been freshly created, it may cause
         * the monster to upgrade itself into another monster type.
         */
        void on_load();

        const pathfinding_settings &get_legacy_pathfinding_settings() const override;
        std::set<tripoint> get_legacy_path_avoid() const override;

        std::pair<PathfindingSettings, RouteSettings> get_pathfinding_pair() const override;

        // summoned monsters via spells
        void set_summon_time( const time_duration &length );
        // handles removing the monster if the timer runs out
        void decrement_summon_timer();

        item *get_tack_item() const;
        detached_ptr<item> set_tack_item( detached_ptr<item> &&to );
        detached_ptr<item> remove_tack_item( );

        item *get_tied_item() const;
        detached_ptr<item> set_tied_item( detached_ptr<item> &&to );
        detached_ptr<item> remove_tied_item( );

        item *get_armor_item() const;
        detached_ptr<item> set_armor_item( detached_ptr<item> &&to );
        detached_ptr<item> remove_armor_item( );

        item *get_storage_item() const;
        detached_ptr<item> set_storage_item( detached_ptr<item> &&to );
        detached_ptr<item> remove_storage_item( );

        item *get_battery_item() const;
        detached_ptr<item> set_battery_item( detached_ptr<item> &&to );
        detached_ptr<item> remove_battery_item( );

        void add_corpse_component( detached_ptr<item> &&it );
        detached_ptr<item> remove_corpse_component( item &it );
        std::vector<detached_ptr<item>> remove_corpse_components();

    private:
        void process_trigger( mon_trigger trig, int amount );
        void process_trigger( mon_trigger trig, const std::function<int()> &amount_func );

        void trigger_character_aggro( const char *reason );
        void trigger_character_aggro_chance( int chance, const char *reason );

        location_vector<item> corpse_components; // Hack to make bionic corpses generate CBMs on death

    private:
        int hp;
        std::map<std::string, mon_special_attack> special_attacks;
        tripoint goal;
        tripoint position;
        bool dead;
        /** Legacy loading logic for monsters that are packing ammo. **/
        void normalize_ammo( int old_ammo );
        /** Normal upgrades **/
        int next_upgrade_time();
        bool upgrades;
        int upgrade_time;
        bool reproduces;
        std::optional<time_point> baby_timer;
        time_point udder_timer;
        monster_horde_attraction horde_attraction;
        /** Found path. Note: Not used by monsters that don't pathfind! **/
        std::vector<tripoint> path;
        bool repath_requested = false;
        std::bitset<NUM_MEFF> effect_cache;
        std::optional<time_duration> summon_time_limit = std::nullopt;


        player *find_dragged_foe();
        void nursebot_operate( player *dragged_foe );

    protected:
        location_ptr<item, false> tied_item; // item used to tie the monster
        location_ptr<item, false> tack_item; // item representing saddle and reins and such
        location_ptr<item, false> armor_item; // item of armor the monster may be wearing
        location_ptr<item, false> storage_item; // storage item for monster carrying items
        location_ptr<item, false> battery_item; // item to power mechs
        location_vector<item> inv; // Inventory
        void store( JsonOut &json ) const;
        void load( const JsonObject &data );

        /** Processes monster-specific effects of an effect. */
        void process_one_effect( effect &it, bool is_new ) override;
};


