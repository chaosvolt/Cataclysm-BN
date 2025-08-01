#pragma once

#include <array>
#include <bitset>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "coordinate_conversions.h"
#include "coordinates.h"
#include "enums.h"
#include "filter_utils.h"
#include "game_constants.h"
#include "item.h"
#include "item_stack.h"
#include "lightmap.h"
#include "line.h"
#include "lru_cache.h"
#include "mapdata.h"
#include "memory_fast.h"
#include "point.h"
#include "shadowcasting.h"
#include "type_id.h"
#include "units.h"

enum class spawn_disposition;
struct scent_block;
template <typename T> class string_id;

namespace catacurses
{
class window;
} // namespace catacurses
class active_tile_data;
class Character;
class Creature;
class character_id;
class computer;
class field;
class field_entry;
class map_cursor;
class mapgendata;
class monster;
class optional_vpart_position;
class player;
class submap;
template<typename Tripoint>
class tripoint_range;
class vehicle;
class zone_data;
struct maptile;
struct partial_con;
struct trap;

enum class special_item_type : int;
class npc_template;
class tileray;
class vpart_reference;
struct mongroup;
struct projectile;
struct veh_collision;
template<typename T>
class visitable;

struct wrapped_vehicle {
    tripoint pos;
    vehicle *v;
};

using VehicleList = std::vector<wrapped_vehicle>;
class map;

enum ter_bitflags : int;
struct pathfinding_cache;
struct pathfinding_settings;
template<typename T>
struct weighted_int_list;
struct rl_vec2d;


/** Causes all generated maps to be empty grass and prevents saved maps from being loaded, used by the test suite */
extern bool disable_mapgen;

namespace cata
{
template <class T> class poly_serialized;
} // namespace cata

class map_stack : public item_stack
{
    private:
        tripoint location;
        map *myorigin;
    public:
        map_stack( location_vector<item> *newstack, tripoint newloc, map *neworigin ) :
            item_stack( newstack ), location( newloc ), myorigin( neworigin ) {}
        void insert( detached_ptr<item> &&newitem ) override;
        iterator erase( const_iterator it, detached_ptr<item> *out = nullptr ) override;
        detached_ptr<item> remove( item *to_remove ) override;
        int count_limit() const override {
            return MAX_ITEM_IN_SQUARE;
        }
        units::volume max_volume() const override;
};

struct visibility_variables {
    // Is this struct initialized for current z-level
    bool variables_set;
    bool u_sight_impaired;
    bool u_is_boomered;
    // Cached values for map visibility calculations
    int g_light_level;
    int u_clairvoyance;
    float vision_threshold;
};

struct bash_params {
    // Initial strength
    int strength;
    // Make a sound?
    bool silent;
    // Essentially infinite bash strength + some
    bool destroy;
    // Do we want to bash floor if no furn/wall exists?
    bool bash_floor;
    /**
     * Value from 0.0 to 1.0 that affects interpolation between str_min and str_max
     * At 0.0, the bash is against str_min of targeted objects
     * This is required for proper "piercing" bashing, so that one strong hit
     * can destroy a wall and a floor under it rather than only one at a time.
     */
    float roll;
    /*
     * Are we bashing this location from above?
     * Used in determining what sort of terrain the location will turn into,
     * since if we bashed from above and destroyed it, it probably shouldn't
     * have a roof either.
    */
    bool bashing_from_above;
    /**
     * Hack to prevent infinite recursion.
     * TODO: Remove, properly unwrap the calls instead
     */
    bool do_recurse = true;
};

struct bash_results {
    bash_results( bool did_bash, bool success, bool bashed_solid )
        : did_bash( did_bash ), success( success ), bashed_solid( bashed_solid )
    {}
    bash_results() = default;
    // Was anything hit?
    bool did_bash = false;
    // Was anything destroyed?
    bool success = false;
    // Did we bash furniture, terrain or vehicle
    bool bashed_solid = false;
    // If there was recurrent bashing, it will be here
    std::vector<bash_results> subresults;

    bash_results &operator|=( const bash_results &other );
};

/** Draw parameters used by map::drawsq() and similar methods. */
struct drawsq_params {
    private:
        tripoint view_center = tripoint_min;
        bool do_highlight = false;
        bool do_show_items = true;
        bool do_low_light = false;
        bool do_bright_light = false;
        bool do_memorize = false;
        bool do_output = true;

    public:
        constexpr drawsq_params() = default;

        /**
         * Highlight the tile. On TILES, draws an overlay; on CURSES, inverts color.
         * Default: false.
         */
        //@{
        constexpr drawsq_params &highlight( bool v ) {
            do_highlight = v;
            return *this;
        }
        constexpr bool highlight() const {
            return do_highlight;
        }
        //@}

        /**
         * Whether to draw items on the tile.
         * Default: true.
         */
        //@{
        constexpr drawsq_params &show_items( bool v ) {
            do_show_items = v;
            return *this;
        }
        constexpr bool show_items() const {
            return do_show_items;
        }
        //@}

        /**
         * Whether tile is low light, and should be drawn with muted color.
         * Default: false.
         */
        //@{
        constexpr drawsq_params &low_light( bool v ) {
            do_low_light = v;
            return *this;
        }
        constexpr bool low_light() const {
            return do_low_light;
        }
        //@}

        /**
         * Whether tile is in bright light. Affects NV overlay, and nothing else.
         * Default: false;
         */
        //@{
        constexpr drawsq_params &bright_light( bool v ) {
            do_bright_light = v;
            return *this;
        }
        constexpr bool bright_light() const {
            return do_bright_light;
        }
        //@}

        /**
         * Whether the tile should be memorized. Used only in map::draw().
         * Default: false.
         */
        //@{
        constexpr drawsq_params &memorize( bool v ) {
            do_memorize = v;
            return *this;
        }
        constexpr bool memorize() const {
            return do_memorize;
        }
        //@}

        /**
         * HACK: Whether the tile should be printed. Used only in map::draw()
         * as a hack for memorizing off-screen tiles.
         * Default: true.
         */
        //@{
        constexpr drawsq_params &output( bool v ) {
            do_output = v;
            return *this;
        }
        constexpr bool output() const {
            return do_output;
        }
        //@}

        /**
         * Set view center.
         * Default: uses avatar's current view center.
         */
        //@{
        constexpr drawsq_params &center( const tripoint &p ) {
            view_center = p;
            return *this;
        }
        constexpr drawsq_params &center_at_avatar() {
            view_center = tripoint_min;
            return *this;
        }
        tripoint center() const;
        //@}
};

//This is included in the global namespace rather than within level_cache as c++ doesn't allow forward declarations within a namespace
struct diagonal_blocks {
    bool nw;
    bool ne;
};

struct level_cache {
    // Zeros all relevant values
    level_cache();
    level_cache( const level_cache &other ) = default;

    std::bitset<MAPSIZE *MAPSIZE> transparency_cache_dirty;
    bool outside_cache_dirty = false;
    bool floor_cache_dirty = false;
    bool seen_cache_dirty = false;
    bool suspension_cache_initialized = false;
    bool suspension_cache_dirty = false;
    std::list<point> suspension_cache;

    four_quadrants lm[MAPSIZE_X][MAPSIZE_Y];
    float sm[MAPSIZE_X][MAPSIZE_Y];
    // To prevent redundant ray casting into neighbors: precalculate bulk light source positions.
    // This is only valid for the duration of generate_lightmap
    float light_source_buffer[MAPSIZE_X][MAPSIZE_Y];

    // if false, means tile is under the roof ("inside"), true means tile is "outside"
    // "inside" tiles are protected from sun, rain, etc. (see "INDOORS" flag)
    bool outside_cache[MAPSIZE_X][MAPSIZE_Y];

    // true when vehicle below has "ROOF" or "OPAQUE" part, furniture below has "SUN_ROOF_ABOVE"
    //      or terrain doesn't have "NO_FLOOR" flag
    // false otherwise
    // i.e. true == has floor
    bool floor_cache[MAPSIZE_X][MAPSIZE_Y];

    // stores cached transparency of the tiles
    // units: "transparency" (see LIGHT_TRANSPARENCY_OPEN_AIR)
    float transparency_cache[MAPSIZE_X][MAPSIZE_Y];

    // true when light entering a tile diagonally is blocked by the walls of a turned vehicle. The direction is the direction that the light must be travelling.
    // check the nw value of x+1, y+1 to find the se value of a tile and the ne of x-1, y+1 for sw
    diagonal_blocks vehicle_obscured_cache[MAPSIZE_X][MAPSIZE_Y];

    // same as above but for obstruction rather than light
    diagonal_blocks vehicle_obstructed_cache[MAPSIZE_X][MAPSIZE_Y];

    // stores "visibility" of the tiles to the player
    // values range from 1 (fully visible to player) to 0 (not visible)
    float seen_cache[MAPSIZE_X][MAPSIZE_Y];

    // same as `seen_cache` (same units) but contains values for cameras and mirrors
    // effective "visibility_cache" is calculated as "max(seen_cache, camera_cache)"
    float camera_cache[MAPSIZE_X][MAPSIZE_Y];

    // stores resulting apparent brightness to player, calculated by map::apparent_light_at
    lit_level visibility_cache[MAPSIZE_X][MAPSIZE_Y];
    std::bitset<MAPSIZE_X *MAPSIZE_Y> map_memory_seen_cache;
    std::bitset<MAPSIZE *MAPSIZE> field_cache;

    bool veh_in_active_range;
    bool veh_exists_at[MAPSIZE_X][MAPSIZE_Y];
    std::map< tripoint, std::pair<vehicle *, int> > veh_cached_parts;
    std::set<vehicle *> vehicle_list;
    std::set<vehicle *> zone_vehicles;

};

/**
 * Manage and cache data about a part of the map.
 *
 * Despite the name, this class isn't actually responsible for managing the map as a whole. For that function,
 * see \ref mapbuffer. Instead, this class loads a part of the mapbuffer into a cache, and adds certain temporary
 * information such as lighting calculations to it.
 *
 * To understand the following descriptions better, you should also read \ref map_management
 *
 * The map coordinates always start at (0, 0) for the top-left and end at (map_width-1, map_height-1) for the bottom-right.
 *
 * The actual map data is stored in `submap` instances. These instances are managed by `mapbuffer`.
 * References to the currently active submaps are stored in `map::grid`:
 *     0 1 2
 *     3 4 5
 *     6 7 8
 * In this example, the top-right submap would be at `grid[2]`.
 *
 * When the player moves between submaps, the whole map is shifted, so that if the player moves one submap to the right,
 * (0, 0) now points to a tile one submap to the right from before
 */
class map
{
        friend class editmap;
        friend class visitable<map_cursor>;
        friend class location_visitable<map_cursor>;

    public:
        // Constructors & Initialization
        map( int mapsize = MAPSIZE, bool zlev = true );
        explicit map( bool zlev ) : map( MAPSIZE, zlev ) { }
        virtual ~map();

        map &operator=( const map & ) = delete;
        map &operator=( map && ) noexcept ;

        /**
         * Sets a dirty flag on the a given cache.
         *
         * If this isn't set, it's just assumed that
         * the cache hasn't changed and
         * doesn't need to be updated.
         */
        /*@{*/

        // more granular version of the transparency cache invalidation
        // preferred over map::set_transparency_cache_dirty( const int zlev )
        // p is in local coords ("ms")
        void set_transparency_cache_dirty( const int zlev );

        void set_transparency_cache_dirty( const tripoint &p );

        // invalidates seen cache for the whole zlevel unconditionally

        void set_seen_cache_dirty( const tripoint change_location );

        void set_seen_cache_dirty( const int zlevel );

        void set_outside_cache_dirty( const int zlev );

        void set_floor_cache_dirty( const int zlev );

        void set_suspension_cache_dirty( const int zlev );

        void set_pathfinding_cache_dirty( int zlev );
        /*@}*/

        void set_memory_seen_cache_dirty( const tripoint &p );

        void invalidate_map_cache( const int zlev );

        bool check_seen_cache( const tripoint &p ) const;
        bool check_and_set_seen_cache( const tripoint &p ) const;

        /**
         * Callback invoked when a vehicle has moved.
         */
        void on_vehicle_moved( int smz );

        struct apparent_light_info {
            bool obstructed;
            float apparent_light;
        };
        /** Helper function for light claculation; exposed here for map editor
         */
        static apparent_light_info apparent_light_helper( const level_cache &map_cache,
                const tripoint &p );
        /** Determine the visible light level for a tile, based on light_at
         * for the tile, vision distance, etc
         *
         * @param p The tile on this map to draw.
         * @param cache Currently cached visibility parameters
         */
        lit_level apparent_light_at( const tripoint &p, const visibility_variables &cache ) const;
        visibility_type get_visibility( lit_level ll,
                                        const visibility_variables &cache ) const;

        // See field.cpp
        std::tuple<maptile, maptile, maptile> get_wind_blockers( const int &winddirection,
                const tripoint &pos );

        /** Draw a visible part of the map into `w`.
         *
         * This method uses `g->u.posx()/posy()` for visibility calculations, so it can
         * not be used for anything but the player's viewport. Likewise, only
         * `g->m` and maps with equivalent coordinates can be used, as other maps
         * would have coordinate systems incompatible with `g->u.posx()`
         *
         * @param w Window we are drawing in
         * @param center The coordinate of the center of the viewport, this can
         *               be different from the player coordinate.
         */
        void draw( const catacurses::window &w, const tripoint &center );

        /**
         * Draw the map tile at the given coordinate. Called by `map::draw()`.
         *
         * @param w The window we are drawing in
         * @param p The tile on this map to draw.
         * @param params Draw parameters.
         */
        void drawsq( const catacurses::window &w, const tripoint &p, const drawsq_params &params ) const;

        /**
         * Add currently loaded submaps (in @ref grid) to the @ref mapbuffer.
         * They will than be stored by that class and can be loaded from that class.
         * This can be called several times, the mapbuffer takes care of adding
         * the same submap several times. It should only be called after the map has
         * been loaded.
         * Submaps that have been loaded from the mapbuffer (and not generated) are
         * already stored in the mapbuffer.
         * TODO: determine if this is really needed? Submaps are already in the mapbuffer
         * if they have been loaded from disc and the are added by map::generate, too.
         * So when do they not appear in the mapbuffer?
         */
        void save();
        /**
         * Load submaps into @ref grid. This might create new submaps if
         * the @ref mapbuffer can not deliver the requested submap (as it does
         * not exist on disc).
         * This must be called before the map can be used at all!
         * @param w global coordinates of the submap at grid[0]. This
         * is in submap coordinates.
         * @param update_vehicles If true, add vehicles to the vehicle cache.
         * @param pump_events If true, handle window events during loading. If
         * you set this to true, do ensure that the map is not accessed before
         * this function returns (for example, UIs that draw the map should be
         * disabled).
         */
        void load( const tripoint &w, bool update_vehicles, bool pump_events = false );
        void load( const tripoint_abs_sm &w, bool update_vehicles, bool pump_events = false );
        /**
         * Shift the map along the vector s.
         * This is like loading the map with coordinates derived from the current
         * position of the map (@ref abs_sub) plus the shift vector.
         * Note: the map must have been loaded before this can be called.
         */
        void shift( point s );
        /**
         * Moves the map vertically to (not by!) newz.
         * Does not actually shift anything, only forces cache updates.
         * In the future, it will either actually shift the map or it will get removed
         *  after 3D migration is complete.
         */
        void vertical_shift( int newz );

        void clear_spawns();
        void clear_traps();

        maptile maptile_at( const tripoint &p ) const;
        maptile maptile_at( const tripoint &p );
        maptile maptile_at_internal( const tripoint &p ) const;
        maptile maptile_at_internal( const tripoint &p );
    private:
        // Versions of the above that don't do bounds checks
        std::pair<tripoint, maptile> maptile_has_bounds( const tripoint &p, bool bounds_checked );
        std::array<std::pair<tripoint, maptile>, 8> get_neighbors( const tripoint &p );
        void spread_gas( field_entry &cur, const tripoint &p, int percent_spread,
                         const time_duration &outdoor_age_speedup, scent_block &sblk );
        void create_hot_air( const tripoint &p, int intensity );
        bool gas_can_spread_to( field_entry &cur, const tripoint &src, const tripoint &dst );
        void gas_spread_to( field_entry &cur, maptile &dst, const tripoint &p );
        int burn_body_part( player &u, field_entry &cur, body_part bp, int scale );
    public:

        // Movement and LOS
        /**
        * Calculate the cost to move past the tile at p.
        *
        * The move cost is determined by various obstacles, such
        * as terrain, vehicles and furniture.
        *
        * @note Movement costs for players and zombies both use this function.
        *
        * @return The return value is interpreted as follows:
        * Move Cost | Meaning
        * --------- | -------
        * 0         | Impassable. Use `passable`/`impassable` to check for this.
        * n > 0     | x*n turns to move past this
        */
        int move_cost( const tripoint &p, const vehicle *ignored_vehicle = nullptr ) const;
        int move_cost( point p, const vehicle *ignored_vehicle = nullptr ) const {
            return move_cost( tripoint( p, abs_sub.z ), ignored_vehicle );
        }
        /**
         * Internal versions of public functions to avoid checking same variables multiple times.
         * They lack safety checks, because their callers already do those.
         */
        int move_cost_internal( const furn_t &furniture, const ter_t &terrain,
                                const vehicle *veh, int vpart ) const;
        bool impassable( const tripoint &p ) const;
        bool impassable( point p ) const {
            return !passable( p );
        }
        bool passable( const tripoint &p ) const;
        bool passable( point p ) const {
            return passable( tripoint( p, abs_sub.z ) );
        }
        bool is_wall_adjacent( const tripoint &center ) const;

        /**
        * Similar behavior to `move_cost()`, but ignores vehicles.
        */
        int move_cost_ter_furn( const tripoint &p ) const;
        int move_cost_ter_furn( point p ) const {
            return move_cost_ter_furn( tripoint( p, abs_sub.z ) );
        }
        bool impassable_ter_furn( const tripoint &p ) const;
        bool passable_ter_furn( const tripoint &p ) const;

        /**
        * Cost to move out of one tile and into the next.
        *
        * @return The cost in turns to move out of tripoint `from` and into `to`
        */
        int combined_movecost( const tripoint &from, const tripoint &to,
                               const vehicle *ignored_vehicle = nullptr,
                               int modifier = 0, bool flying = false, bool via_ramp = false ) const;

        /**
         * Returns true if a creature could walk from `from` to `to` in one step.
         * That is, if the tiles are adjacent and either on the same z-level or connected
         * by stairs or (in case of flying monsters) open air with no floors.
         */
        bool valid_move( const tripoint &from, const tripoint &to,
                         bool bash = false, bool flying = false, bool via_ramp = false ) const;

        /**
         * Size of map objects at `p` for purposes of ranged combat.
         * Size is in percentage of tile: if 1.0, all attacks going through tile
         * should hit map objects on it, if 0.0 there is nothing to be hit (air/water).
         */
        double ranged_target_size( const tripoint &p ) const;

        // Sees:
        /**
        * Returns whether `F` sees `T` with a view range of `range`.
        */
        bool sees( const tripoint &F, const tripoint &T, int range ) const;
    private:
        /**
         * Don't expose the slope adjust outside map functions.
         *
         * @param F Thing doing the seeing
         * @param T Thing being seen
         * @param range Vision range of F
         * @param bresenham_slope Indicates the start offset of Bresenham line used to connect
         * the two points, and may subsequently be used to form a path between them.
         * Set to zero if the function returns false.
        **/
        bool sees( const tripoint &F, const tripoint &T, int range, int &bresenham_slope ) const;
    public:
        /**
        * Returns coverage of target in relation to the observer. Target is loc2, observer is loc1.
        * First tile from the target is an obstacle, which has the coverage value.
        * If there's no obstacle adjacent to the target - no coverage.
        */
        int obstacle_coverage( const tripoint &loc1, const tripoint &loc2 ) const;
        /**
        * Returns coverage value of the tile.
        */
        int coverage( const tripoint &p ) const;
        /**
         * Check whether there's a direct line of sight between `F` and
         * `T` with the additional movecost restraints.
         *
         * Checks two things:
         * 1. The `sees()` algorithm between `F` and `T`
         * 2. That moving over the line of sight would have a move_cost between
         *    `cost_min` and `cost_max`.
         */
        bool clear_path( const tripoint &f, const tripoint &t, int range,
                         int cost_min, int cost_max ) const;

        /**
         * Checks if a rotated vehicle is blocking diagonal movement, tripoints must be adjacent
         */
        bool obstructed_by_vehicle_rotation( const tripoint &from, const tripoint &to ) const;

        /**
         * Checks if a rotated vehicle is blocking diagonal vision, tripoints must be adjacent
         */
        bool obscured_by_vehicle_rotation( const tripoint &from, const tripoint &to ) const;

        /**
         * Populates a vector of points that are reachable within a number of steps from a
         * point. It could be generalized to take advantage of z levels, but would need some
         * additional code to detect whether a valid transition was on a tile.
         *
         * Does the following:
         * 1. Checks if a point is reachable using a flood fill and if it is, adds it to a vector.
         *
         */
        void reachable_flood_steps( std::vector<tripoint> &reachable_pts, const tripoint &f,
                                    int range,
                                    int cost_min, int cost_max ) const;

        /**
         * Iteratively tries Bresenham lines with different biases
         * until it finds a clear line or decides there isn't one.
         * returns the line found, which may be the straight line, but blocked.
         */
        std::vector<tripoint> find_clear_path( const tripoint &source, const tripoint &destination ) const;

        /**
         * Check whether the player can access the items located @p. Certain furniture/terrain
         * may prevent that (e.g. a locked safe).
         */
        bool accessible_items( const tripoint &t ) const;
        /**
         * Calculate next search points surrounding the current position.
         * Points closer to the target come first.
         * This method leads to straighter lines and prevents weird looking movements away from the target.
         */
        std::vector<tripoint> get_dir_circle( const tripoint &f, const tripoint &t ) const;

        /**
         * Calculate the best path using A*
         *
         * @param f The source location from which to path.
         * @param t The destination to which to path.
         * @param settings Structure describing pathfinding parameters.
         * @param pre_closed Never path through those points. They can still be the source or the destination.
         */
        std::vector<tripoint> route( const tripoint &f, const tripoint &t,
                                     const pathfinding_settings &settings,
        const std::set<tripoint> &pre_closed = {{ }} ) const;

        // Vehicles: Common to 2D and 3D
        VehicleList get_vehicles();
        void add_vehicle_to_cache( vehicle * );
        void clear_vehicle_point_from_cache( vehicle *veh, const tripoint &pt );
        void update_vehicle_cache( vehicle *, int old_zlevel );
        void reset_vehicle_cache( );
        void clear_vehicle_cache( );
        void clear_vehicle_list( int zlev );
        void update_vehicle_list( const submap *to, int zlev );
        //Returns true if vehicle zones are dirty and need to be recached
        bool check_vehicle_zones( int zlev );
        std::vector<zone_data *> get_vehicle_zones( int zlev );
        void register_vehicle_zone( vehicle *, int zlev );
        bool deregister_vehicle_zone( zone_data &zone );

        // Removes vehicle from map and returns it in unique_ptr
        std::unique_ptr<vehicle> detach_vehicle( vehicle *veh );
        void destroy_vehicle( vehicle *veh );
        // Vehicle movement
        void vehmove();
        // Selects a vehicle to move, returns false if no moving vehicles
        bool vehproceed( VehicleList &vehicle_list );

        // Vehicles
        VehicleList get_vehicles( const tripoint &start, const tripoint &end );
        /**
        * Checks if tile is occupied by vehicle and by which part.
        *
        * @param p Tile to check for vehicle
        */
        optional_vpart_position veh_at( const tripoint &p ) const;
        optional_vpart_position veh_at( const tripoint_abs_ms &p ) const;
        vehicle *veh_at_internal( const tripoint &p, int &part_num );
        const vehicle *veh_at_internal( const tripoint &p, int &part_num ) const;
        // Put player on vehicle at x,y
        void board_vehicle( const tripoint &p, Character *pl );
        // Remove given passenger from given vehicle part.
        // If dead_passenger, then null passenger is acceptable.
        void unboard_vehicle( const vpart_reference &, Character *passenger,
                              bool dead_passenger = false );
        // Remove passenger from vehicle at p.
        void unboard_vehicle( const tripoint &p, bool dead_passenger = false );
        // Change vehicle coordinates and move vehicle's driver along.
        // WARNING: not checking collisions!
        bool displace_vehicle( vehicle &veh, const tripoint &dp );

        // Shift the vehicle's z-level without moving any parts
        void shift_vehicle_z( vehicle &veh, int z_shift );
        // move water under wheels. true if moved
        bool displace_water( const tripoint &dp );

        // Returns the wheel area of the vehicle multiplied by traction of the surface
        // When ignore_movement_modifiers is set to true, it returns the area of the wheels touching the ground
        // TODO: Remove the ugly sinking vehicle hack
        float vehicle_wheel_traction( const vehicle &veh, bool ignore_movement_modifiers = false ) const;

        // Executes vehicle-vehicle collision based on vehicle::collision results
        // Returns impulse of the executed collision
        // If vector contains collisions with vehicles other than veh2, they will be ignored
        float vehicle_vehicle_collision( vehicle &veh, vehicle &veh2,
                                         const std::vector<veh_collision> &collisions );
        // Throws vehicle passengers about the vehicle, possibly out of it
        // Returns change in vehicle orientation due to lost control
        units::angle shake_vehicle( vehicle &veh, int velocity_before, units::angle direction );

        // Actually moves the vehicle
        // Unlike displace_vehicle, this one handles collisions
        vehicle *move_vehicle( vehicle &veh, const tripoint &dp, const tileray &facing );

        // Furniture
        void set( const tripoint &p, const ter_id &new_terrain, const furn_id &new_furniture );
        void set( point p, const ter_id &new_terrain, const furn_id &new_furniture ) {
            furn_set( p, new_furniture );
            ter_set( p, new_terrain );
        }
        std::string name( const tripoint &p );
        std::string name( point p ) {
            return name( tripoint( p, abs_sub.z ) );
        }
        std::string disp_name( const tripoint &p );
        /**
        * Returns the name of the obstacle at p that might be blocking movement/projectiles/etc.
        * Note that this only accounts for vehicles, terrain, and furniture.
        */
        std::string obstacle_name( const tripoint &p );
        bool has_furn( const tripoint &p ) const;
        bool has_furn( point p ) const {
            return has_furn( tripoint( p, abs_sub.z ) );
        }
        furn_id furn( const tripoint &p ) const;
        furn_id furn( point p ) const {
            return furn( tripoint( p, abs_sub.z ) );
        }
        /**
        * Sets the furniture at given position.
        *
        * @param p Position within the map
        * @param new_furniture Id of new furniture
        * @param new_active Override default active tile of new furniture
        */
        void furn_set( const tripoint &p, const furn_id &new_furniture,
                       const cata::poly_serialized<active_tile_data> &new_active = nullptr );
        void furn_set( point p, const furn_id &new_furniture ) {
            furn_set( tripoint( p, abs_sub.z ), new_furniture );
        }
        std::string furnname( const tripoint &p );
        std::string furnname( point p ) {
            return furnname( tripoint( p, abs_sub.z ) );
        }
        bool can_move_furniture( const tripoint &pos, player *p = nullptr );

        // Terrain
        ter_id ter( const tripoint &p ) const;
        ter_id ter( point p ) const {
            return ter( tripoint( p, abs_sub.z ) );
        }

        // Return a bitfield of the adjacent tiles which connect to the given
        // connect_group.  From least-significant bit the order is south, east,
        // west, north (because that's what cata_tiles expects).
        // Based on a combination of visibility and memory, not simply the true
        // terrain. Additional overrides can be passed in to override terrain
        // at specific positions. This is used to display terrain overview in
        // the map editor.
        uint8_t get_known_connections( const tripoint &p, int connect_group,
                                       const std::map<tripoint, ter_id> &override = {} ) const;
        /**
         * Returns the full harvest list, for spawning.
         */
        // as above, but for furniture
        uint8_t get_known_connections_f( const tripoint &p, int connect_group,
                                         const std::map<tripoint, furn_id> &override = {} ) const;

        const harvest_id &get_harvest( const tripoint &p ) const;
        /**
         * Returns names of the items that would be dropped.
         */
        const std::set<std::string> &get_harvest_names( const tripoint &p ) const;
        ter_id get_ter_transforms_into( const tripoint &p ) const;
        furn_id get_furn_transforms_into( const tripoint &p ) const;

        bool ter_set( const tripoint &p, const ter_id &new_terrain );
        bool ter_set( point p, const ter_id &new_terrain ) {
            return ter_set( tripoint( p, abs_sub.z ), new_terrain );
        }

        std::string tername( const tripoint &p ) const;
        std::string tername( point p ) const {
            return tername( tripoint( p, abs_sub.z ) );
        }

        // Check for terrain/furniture/field that provide a
        // "fire" item to be used for example when crafting or when
        // a iuse function needs fire.
        bool has_nearby_fire( const tripoint &p, int radius = 1 );
        /**
         * Check whether a table/workbench/vehicle kitchen or other flat
         * surface is nearby that could be used for crafting or eating.
         */
        bool has_nearby_table( const tripoint &p, int radius = 1 );
        /**
         * Check whether a chair or vehicle seat is nearby.
         */
        bool has_nearby_chair( const tripoint &p, int radius = 1 );
        /**
         * Check if creature can see some items at p. Includes:
         * - check for items at this location (has_items(p))
         * - check for SEALED flag (sealed furniture/terrain makes
         * items not visible under any circumstances).
         * - check for CONTAINER flag (makes items only visible when
         * the creature is at p or at an adjacent square).
         */
        bool sees_some_items( const tripoint &p, const Creature &who ) const;
        bool sees_some_items( const tripoint &p, const tripoint &from ) const;
        /**
         * Check if the creature could see items at p if there were
         * any items. This is similar to @ref sees_some_items, but it
         * does not check that there are actually any items.
         */
        bool could_see_items( const tripoint &p, const Creature &who ) const;
        bool could_see_items( const tripoint &p, const tripoint &from ) const;
        /**
         * Checks for existence of items. Faster than i_at(p).empty
         */
        bool has_items( const tripoint &p ) const;

        /**
         * Calls the examine function of furniture or terrain at given tile, for given character.
         * Will only examine terrain if furniture had @ref iexamine::none as the examine function.
         */
        void examine( Character &who, const tripoint &pos );

        /**
         * Returns true if point at pos is harvestable right now, with no extra tools.
         */
        bool is_harvestable( const tripoint &pos ) const;

        // Flags
        // Words relevant to terrain (sharp, etc)
        std::string features( const tripoint &p );
        std::string features( point p ) {
            return features( tripoint( p, abs_sub.z ) );
        }
        // Checks terrain, furniture and vehicles
        bool has_flag( const std::string &flag, const tripoint &p ) const;
        bool has_flag( const std::string &flag, point p ) const {
            return has_flag( flag, tripoint( p, abs_sub.z ) );
        }
        // True if items can be dropped in this tile
        bool can_put_items( const tripoint &p ) const;
        bool can_put_items( point p ) const {
            return can_put_items( tripoint( p, abs_sub.z ) );
        }
        // True if items can be placed in this tile
        bool can_put_items_ter_furn( const tripoint &p ) const;
        bool can_put_items_ter_furn( point p ) const {
            return can_put_items_ter_furn( tripoint( p, abs_sub.z ) );
        }
        // Checks terrain
        bool has_flag_ter( const std::string &flag, const tripoint &p ) const;
        bool has_flag_ter( const std::string &flag, point p ) const {
            return has_flag_ter( flag, tripoint( p, abs_sub.z ) );
        }
        // Checks furniture
        bool has_flag_furn( const std::string &flag, const tripoint &p ) const;
        bool has_flag_furn( const std::string &flag, point p ) const {
            return has_flag_furn( flag, tripoint( p, abs_sub.z ) );
        }
        // Checks vehicle part flag
        bool has_flag_vpart( const std::string &flag, const tripoint &p ) const;
        // Checks vehicle part or furniture
        bool has_flag_furn_or_vpart( const std::string &flag, const tripoint &p ) const;
        // Checks terrain or furniture
        bool has_flag_ter_or_furn( const std::string &flag, const tripoint &p ) const;
        bool has_flag_ter_or_furn( const std::string &flag, point p ) const {
            return has_flag_ter_or_furn( flag, tripoint( p, abs_sub.z ) );
        }
        // Fast "oh hai it's update_scent/lightmap/draw/monmove/self/etc again, what about this one" flag checking
        // Checks terrain, furniture and vehicles
        bool has_flag( ter_bitflags flag, const tripoint &p ) const;
        bool has_flag( ter_bitflags flag, point p ) const {
            return has_flag( flag, tripoint( p, abs_sub.z ) );
        }
        // Checks terrain
        bool has_flag_ter( ter_bitflags flag, const tripoint &p ) const;
        bool has_flag_ter( ter_bitflags flag, point p ) const {
            return has_flag_ter( flag, tripoint( p, abs_sub.z ) );
        }
        // Checks furniture
        bool has_flag_furn( ter_bitflags flag, const tripoint &p ) const;
        bool has_flag_furn( ter_bitflags flag, point p ) const {
            return has_flag_furn( flag, tripoint( p, abs_sub.z ) );
        }
        // Checks terrain or furniture
        bool has_flag_ter_or_furn( ter_bitflags flag, const tripoint &p ) const;
        bool has_flag_ter_or_furn( ter_bitflags flag, point p ) const {
            return has_flag_ter_or_furn( flag, tripoint( p, abs_sub.z ) );
        }

        // Bashable
        /** Returns true if there is a bashable vehicle part or the furn/terrain is bashable at p */
        bool is_bashable( const tripoint &p, bool allow_floor = false ) const;
        bool is_bashable( point p ) const {
            return is_bashable( tripoint( p, abs_sub.z ) );
        }
        /** Returns true if the terrain at p is bashable */
        bool is_bashable_ter( const tripoint &p, bool allow_floor = false ) const;
        bool is_bashable_ter( point p ) const {
            return is_bashable_ter( tripoint( p, abs_sub.z ) );
        }
        /** Returns true if the furniture at p is bashable */
        bool is_bashable_furn( const tripoint &p ) const;
        bool is_bashable_furn( point p ) const {
            return is_bashable_furn( tripoint( p, abs_sub.z ) );
        }
        /** Returns true if the furniture or terrain at p is bashable */
        bool is_bashable_ter_furn( const tripoint &p, bool allow_floor = false ) const;
        bool is_bashable_ter_furn( point p ) const {
            return is_bashable_ter_furn( tripoint( p, abs_sub.z ) );
        }
        /** Returns max_str of the furniture or terrain at p */
        int bash_strength( const tripoint &p, bool allow_floor = false ) const;
        int bash_strength( point p ) const {
            return bash_strength( tripoint( p, abs_sub.z ) );
        }
        /** Returns min_str of the furniture or terrain at p */
        int bash_resistance( const tripoint &p, bool allow_floor = false ) const;
        int bash_resistance( point p ) const {
            return bash_resistance( tripoint( p, abs_sub.z ) );
        }
        /** Returns a success rating from -1 to 10 for a given tile based on a set strength, used for AI movement planning
        *  Values roughly correspond to 10% increment chances of success on a given bash, rounded down. -1 means the square is not bashable */
        int bash_rating( int str, const tripoint &p, bool allow_floor = false ) const;
        int bash_rating( const int str, point p ) const {
            return bash_rating( str, tripoint( p, abs_sub.z ) );
        }
        int bash_rating_internal( int str, const furn_t &furniture,
                                  const ter_t &terrain, bool allow_floor,
                                  const vehicle *veh, int part ) const;


        // Rubble
        /** Generates rubble at the given location, if overwrite is true it just writes on top of what currently exists
         *  floor_type is only used if there is a non-bashable wall at the location or with overwrite = true */
        void make_rubble( const tripoint &p, const furn_id &rubble_type,
                          const ter_id &floor_type, bool overwrite = false );
        void make_rubble( const tripoint &p, const furn_id &rubble_type ) {
            make_rubble( p, rubble_type, t_dirt, false );
        }
        void make_rubble( const tripoint &p ) {
            make_rubble( p, f_rubble, t_dirt, false );
        }

        bool is_outside( const tripoint &p ) const;
        bool is_outside( point p ) const {
            return is_outside( tripoint( p, abs_sub.z ) );
        }
        /**
         * Returns whether or not the terrain at the given location can be dived into
         * (by monsters that can swim or are aquatic or non-breathing).
         * @param p The coordinate to look at.
         * @return true if the terrain can be dived into; false if not.
         */
        bool is_divable( const tripoint &p ) const;
        bool is_divable( point p ) const {
            return is_divable( tripoint( p, abs_sub.z ) );
        }
        bool is_water_shallow_current( const tripoint &p ) const;
        bool is_water_shallow_current( point p ) const {
            return is_water_shallow_current( tripoint( p, abs_sub.z ) );
        }

        /** Check if the last terrain is wall in direction NORTH, SOUTH, WEST or EAST
         *  @param no_furn if true, the function will stop and return false
         *  if it encounters a furniture
         *  @param p starting coordinates of check
         *  @param max ending coordinates of check
         *  @param dir Direction of check
         *  @return true if from x to xmax or y to ymax depending on direction
         *  all terrain is floor and the last terrain is a wall */
        bool is_last_ter_wall( bool no_furn, point p,
                               point max, direction dir ) const;

        /**
         * Checks if there are any tinder flagged items on the tile.
         * @param p tile to check
         */
        bool tinder_at( const tripoint &p );

        /**
         * Checks if there are any flammable items on the tile.
         * @param p tile to check
         * @param threshold Fuel threshold (lower means worse fuels are accepted).
         */
        bool flammable_items_at( const tripoint &p, int threshold = 0 );
        /** Returns true if there is a flammable item or field or the furn/terrain is flammable at p */
        bool is_flammable( const tripoint &p );
        point random_outdoor_tile();
        // mapgen

        void draw_line_ter( const ter_id &type, point p1, point p2 );
        void draw_line_furn( const furn_id &type, point p1, point p2 );
        void draw_fill_background( const ter_id &type );
        void draw_fill_background( ter_id( *f )() );
        void draw_fill_background( const weighted_int_list<ter_id> &f );

        void draw_square_ter( const ter_id &type, point p1, point p2 );
        void draw_square_furn( const furn_id &type, point p1, point p2 );
        void draw_square_ter( ter_id( *f )(), point p1, point p2 );
        void draw_square_ter( const weighted_int_list<ter_id> &f, point p1,
                              point p2 );
        void draw_rough_circle_ter( const ter_id &type, point p, int rad );
        void draw_rough_circle_furn( const furn_id &type, point p, int rad );
        void draw_circle_ter( const ter_id &type, const rl_vec2d &p, double rad );
        void draw_circle_ter( const ter_id &type, point p, int rad );
        void draw_circle_furn( const furn_id &type, point p, int rad );

        void add_corpse( const tripoint &p );

        // Terrain changing functions
        // Change all instances of $from->$to
        void translate( const ter_id &from, const ter_id &to );
        // Change all instances $from->$to within this radius, optionally limited to locations in the same submap.
        // Optionally toggles instances $from->$to & $to->$from
        void translate_radius( const ter_id &from, const ter_id &to, float radi, const tripoint &p,
                               bool same_submap = false, bool toggle_between = false );
        bool close_door( const tripoint &p, bool inside, bool check_only );
        bool open_door( const tripoint &p, bool inside, bool check_only = false );
        // Destruction
        /** bash a square for a set number of times at set power.  Does not destroy */
        void batter( const tripoint &p, int power, int tries = 1, bool silent = false );
        /** Keeps bashing a square until it can't be bashed anymore */
        void destroy( const tripoint &p, bool silent = false );
        /** Keeps bashing a square until there is no more furniture */
        void destroy_furn( const tripoint &p, bool silent = false );
        void crush( const tripoint &p );
        void shoot( const tripoint &origin, const tripoint &p, projectile &proj, bool hit_items );
        /** Checks if a square should collapse, returns the X for the one_in(X) collapse chance */
        int collapse_check( const tripoint &p );
        /** Causes a collapse at p, such as from destroying a wall */
        void collapse_at( const tripoint &p, bool silent, bool was_supporting = false,
                          bool destroy_pos = true );
        /** Checks surrounding tiles for suspension, and has them check for collapse. !!Should only be called after the tile at this point has been destroyed!!*/
        void propagate_suspension_check( const tripoint &point );
        /** Triggers a recursive collapse of suspended tiles based on their support validity*/
        void collapse_invalid_suspension( const tripoint &point );
        /** Checks the four orientations in which a suspended tile could be valid, and returns if the tile is valid*/
        bool is_suspension_valid( const tripoint &point );
        /** Tries to smash the trap at the given tripoint. */
        void smash_trap( const tripoint &p, const int power, const std::string &cause_message );
        /** Tries to smash the items at the given tripoint. */
        void smash_items( const tripoint &p, int power, const std::string &cause_message, bool do_destroy );
        /**
         * Returns a pair where first is whether anything was smashed and second is if it was destroyed.
         *
         * @param p Where to bash
         * @param str How hard to bash
         * @param silent Don't produce any sound
         * @param destroy Destroys some otherwise unbashable tiles
         * @param bash_floor Allow bashing the floor and the tile that supports it
         * @param bashing_vehicle Vehicle that should NOT be bashed (because it is doing the bashing)
         */
        bash_results bash( const tripoint &p, int str, bool silent = false,
                           bool destroy = false, bool bash_floor = false,
                           const vehicle *bashing_vehicle = nullptr );

        bash_results bash_vehicle( const tripoint &p, const bash_params &params );
        bash_results bash_ter_furn( const tripoint &p, const bash_params &params );

        // Effects of attacks/items
        bool hit_with_acid( const tripoint &p );
        bool hit_with_fire( const tripoint &p );

        /**
         * Returns true if there is furniture for which filter returns true in a 1 tile radius of p.
         * Pass return_true<furn_t> to detect all adjacent furniture.
         * @param p the location to check at
         * @param filter what to filter the furniture by.
         */
        bool has_adjacent_furniture_with( const tripoint &p,
                                          const std::function<bool( const furn_t & )> &filter );
        /**
         * Remove moppable fields/items at this location
         *  @param p the location
         *  @return true if anything moppable was there, false otherwise.
         */
        bool mop_spills( const tripoint &p );
        /**
         * Moved here from weather.cpp for speed. Decays fire, washable fields and scent.
         * Washable fields are decayed only by 1/3 of the amount fire is.
         */
        void decay_fields_and_scent( const time_duration &amount );

        // Signs
        std::string get_signage( const tripoint &p ) const;
        void set_signage( const tripoint &p, const std::string &message ) const;
        void delete_signage( const tripoint &p ) const;

        // Radiation
        int get_radiation( const tripoint &p ) const;
        void set_radiation( const tripoint &p, int value );
        void set_radiation( point p, const int value ) {
            set_radiation( tripoint( p, abs_sub.z ), value );
        }

        /** Increment the radiation in the given tile by the given delta
        *  (decrement it if delta is negative)
        */
        void adjust_radiation( const tripoint &p, int delta );
        void adjust_radiation( point p, const int delta ) {
            adjust_radiation( tripoint( p, abs_sub.z ), delta );
        }

        // Temperature
        // Temperature for submap
        int get_temperature( const tripoint &p ) const;
        // Set temperature for all four submap quadrants
        void set_temperature( const tripoint &p, int temperature );
        void set_temperature( point p, int new_temperature ) {
            set_temperature( tripoint( p, abs_sub.z ), new_temperature );
        }

        // Returns points for all submaps with inconsistent state relative to
        // the list in map.  Used in tests.
        std::vector<tripoint> check_submap_active_item_consistency();
        // Accessor that returns a wrapped reference to an item stack for safe modification.
        map_stack i_at( const tripoint &p );
        map_stack i_at( point p ) {
            return i_at( tripoint( p, abs_sub.z ) );
        }
        detached_ptr<item> water_from( const tripoint &p );
        std::vector<detached_ptr<item>> i_clear( const tripoint &p );
        std::vector<detached_ptr<item>> i_clear( point p ) {
            return i_clear( tripoint( p, abs_sub.z ) );
        }
        // i_rem() methods that return values act like container::erase(),
        // returning an iterator to the next item after removal.
        map_stack::iterator i_rem( const tripoint &p, map_stack::const_iterator it,
                                   detached_ptr<item> *out = nullptr );
        map_stack::iterator i_rem( point location, map_stack::const_iterator it,
                                   detached_ptr<item> *out = nullptr ) {
            return i_rem( tripoint( location, abs_sub.z ), it, out );
        }

        detached_ptr<item> i_rem( const tripoint &p, item *it );
        detached_ptr<item> i_rem( point p, item *it ) {
            return i_rem( tripoint( p, abs_sub.z ), it );
        }
        void spawn_artifact( const tripoint &p );
        void spawn_natural_artifact( const tripoint &p, artifact_natural_property prop );
        void spawn_item( const tripoint &p, const itype_id &type_id,
                         unsigned quantity = 1, int charges = 0,
                         const time_point &birthday = calendar::start_of_cataclysm, int damlevel = 0 );
        void spawn_item( point p, const itype_id &type_id,
                         unsigned quantity = 1, int charges = 0,
                         const time_point &birthday = calendar::start_of_cataclysm, int damlevel = 0 ) {
            spawn_item( tripoint( p, abs_sub.z ), type_id, quantity, charges, birthday, damlevel );
        }

        // FIXME: remove these overloads and require spawn_item to take an
        // itype_id
        void spawn_item( const tripoint &p, const std::string &type_id,
                         unsigned quantity = 1, int charges = 0,
                         const time_point &birthday = calendar::start_of_cataclysm, int damlevel = 0 ) {
            spawn_item( p, itype_id( type_id ), quantity, charges, birthday, damlevel );
        }
        void spawn_item( point p, const std::string &type_id,
                         unsigned quantity = 1, int charges = 0,
                         const time_point &birthday = calendar::start_of_cataclysm, int damlevel = 0 ) {
            spawn_item( tripoint( p, abs_sub.z ), type_id, quantity, charges, birthday, damlevel );
        }
        units::volume max_volume( const tripoint &p );
        units::volume free_volume( const tripoint &p );
        units::volume stored_volume( const tripoint &p );

        /**
         *  Adds an item to map tile or stacks charges
         *  @param pos Where to add item
         *  @param obj Item to add
         *  @param overflow if destination is full attempt to drop on adjacent tiles
         *  @return the item if it could not be handled
         *  @warning function is relatively expensive and meant for user initiated actions, not mapgen
         */
        detached_ptr<item> add_item_or_charges( const tripoint &pos, detached_ptr<item> &&obj,
                                                bool overflow = true );
        detached_ptr<item> add_item_or_charges( point p, detached_ptr<item> &&obj, bool overflow = true ) {
            return add_item_or_charges( tripoint( p, abs_sub.z ), std::move( obj ), overflow );
        }

        /**
         * Checks for spawn_rate value for item category of 'itm'.
         * If spawn_rate is less than 1.0, it will make a random roll (0.1-1.0) to check if the item will have a chance to spawn.
         * If spawn_rate is more than or equal to 1.0, it will make item spawn that many times (using roll_remainder).
        */
        float item_category_spawn_rate( const item &itm );

        /**
         * Place an item on the map, despite the parameter name, this is not necessarily a new item.
         * WARNING: does -not- check volume or stack charges. player functions (drop etc) should use
         * map::add_item_or_charges
         *
         * @returns The item that got added, or nulitem.
         */
        void add_item( const tripoint &p, detached_ptr<item> &&new_item );
        void add_item( point p, detached_ptr<item> &&new_item ) {
            add_item( tripoint( p, abs_sub.z ), std::move( new_item ) );
        }
        detached_ptr<item> spawn_an_item( const tripoint &p, detached_ptr<item> &&new_item, int charges,
                                          int damlevel );
        detached_ptr<item> spawn_an_item( point p, detached_ptr<item> &&new_item, int charges,
                                          int damlevel ) {
            return spawn_an_item( tripoint( p, abs_sub.z ), std::move( new_item ), charges, damlevel );
        }


        /**
         * Remove an item from active item processing queue as necessary
         */
        void make_inactive( item &loc );

        /**
         * Update an item's active status, for example when adding
         * hot or perishable liquid to a container.
         * Should be called as part of activate()
         */
        void make_active( item &loc );

        /**
         * Update luminosity before and after item's transformation
         */
        void update_lum( item &loc, bool add );

        /**
         * @name Consume items on the map
         *
         * The functions here consume accessible items / item charges on the map or in vehicles
         * around the player (whose positions is given as origin).
         * They return a list of copies of the consumed items (with the actually consumed charges
         * in it).
         * The quantity / amount parameter will be reduced by the number of items/charges removed.
         * If all required items could be removed from the map, the quantity/amount will be 0,
         * otherwise it will contain a positive value and the remaining items must be gathered from
         * somewhere else.
         */
        /*@{*/
        std::vector<detached_ptr<item>> use_amount_square( const tripoint &p, const itype_id &type,
                                     int &quantity, const std::function<bool( const item & )> &filter = return_true<item> );
        std::vector<detached_ptr<item>> use_amount( const tripoint &origin, int range, const itype_id &type,
                                     int &quantity, const std::function<bool( const item & )> &filter = return_true<item> );
        std::vector<detached_ptr<item>> use_charges( const tripoint &origin, int range,
                                     const itype_id &type,
                                     int &quantity, const std::function<bool( const item & )> &filter = return_true<item> );
        /*@}*/

        /**
        * Place items from item group in the rectangle f - t. Several items may be spawned
        * on different places. Several items may spawn at once (at one place) when the item group says
        * so (uses @ref item_group::items_from which may return several items at once).
        * @param loc Current location of items to be placed
        * @param chance Chance for more items. A chance of 100 creates 1 item all the time, otherwise
        * it's the chance that more items will be created (place items until the random roll with that
        * chance fails). The chance is used for the first item as well, so it may not spawn an item at
        * all. Values <= 0 or > 100 are invalid.
        * @param p1 One corner of rectangle in which to spawn items
        * @param p2 Second corner of rectangle in which to spawn items
        * @param ongrass If false the items won't spawn on flat terrain (grass, floor, ...).
        * @param turn The birthday that the created items shall have.
        * @param magazine percentage chance item will contain the default magazine
        * @param ammo percentage chance item will be filled with default ammo
        * @return vector containing all placed items
        */
        std::vector<item *> place_items( const item_group_id &loc, int chance, const tripoint &p1,
                                         const tripoint &p2, bool ongrass, const time_point &turn,
                                         int magazine = 0, int ammo = 0 );
        std::vector<item *> place_items( const item_group_id &loc, int chance, point p1,
                                         point p2, bool ongrass, const time_point &turn,
                                         int magazine = 0, int ammo = 0 ) {
            return place_items( loc, chance, tripoint( p1, abs_sub.z ), tripoint( p2, abs_sub.z ), ongrass,
                                turn, magazine, ammo );
        }
        /**
        * Place items from an item group at p. Places as much items as the item group says.
        * (Most item groups are distributions and will only create one item.)
        * @param loc Current location of items
        * @param p Destination of items
        * @param turn The birthday that the created items shall have.
        * @return Vector of pointers to placed items (can be empty, but no nulls).
        */
        std::vector<item *> put_items_from_loc( const item_group_id &loc, const tripoint &p,
                                                const time_point &turn = calendar::start_of_cataclysm );

        // Similar to spawn_an_item, but spawns a list of items, or nothing if the list is empty.
        std::vector<detached_ptr<item>> spawn_items( const tripoint &p,
                                     std::vector<detached_ptr<item>> new_items );
        std::vector<detached_ptr<item>> spawn_items( point p, std::vector<detached_ptr<item>> new_items ) {
            return spawn_items( tripoint( p, abs_sub.z ), std::move( new_items ) );
        }

        void create_anomaly( const tripoint &p, artifact_natural_property prop, bool create_rubble = true );
        void create_anomaly( point cp, artifact_natural_property prop, bool create_rubble = true ) {
            create_anomaly( tripoint( cp, abs_sub.z ), prop, create_rubble );
        }

        // Partial construction functions
        void partial_con_set( const tripoint &p, std::unique_ptr<partial_con> con );
        void partial_con_remove( const tripoint &p );
        partial_con *partial_con_at( const tripoint &p );
        // Traps
        void trap_set( const tripoint &p, const trap_id &type );

        const trap &tr_at( const tripoint &p ) const;
        /// See @ref trap::can_see, which is called for the trap here.
        bool can_see_trap_at( const tripoint &p, const Character &c ) const;

        void disarm_trap( const tripoint &p );
        void remove_trap( const tripoint &p );
        const std::vector<tripoint> &get_furn_field_locations() const;
        const std::vector<tripoint> &trap_locations( const trap_id &type ) const;

        // Adds to a list of byproducts from items destroyed in fire.
        void create_burnproducts( std::vector<detached_ptr<item>> &out, const item &fuel,
                                  const units::mass &burned_mass );
        // See fields.cpp
        void process_fields();
        void process_fields_in_submap( submap *current_submap, const tripoint &submap_pos );
        /**
         * Apply field effects to the creature when it's on a square with fields.
         */
        void creature_in_field( Creature &critter );
        /**
         * Apply trap effects to the creature, similar to @ref creature_in_field.
         * If there is no trap at the creatures location, nothing is done.
         * If the creature can avoid the trap, nothing is done as well.
         * Otherwise the trap is triggered.
         * @param critter Creature that just got trapped
         * @param may_avoid If true, the creature tries to avoid the trap
         * (@ref Creature::avoid_trap). If false, the trap is always triggered.
         */
        void creature_on_trap( Creature &critter, bool may_avoid = true );
        // Field functions
        /**
         * Get the fields that are here. This is for querying and looking at it only,
         * one can not change the fields.
         * @param p The local map coordinates, if out of bounds, returns an empty field.
         */
        const field &field_at( const tripoint &p ) const;
        /**
         * Gets fields that are here. Both for querying and edition.
         */
        field &field_at( const tripoint &p );
        /**
         * Get the age of a field entry (@ref field_entry::age), if there is no
         * field of that type, returns `-1_turns`.
         */
        time_duration get_field_age( const tripoint &p, const field_type_id &type ) const;
        /**
         * Get the intensity of a field entry (@ref field_entry::intensity),
         * if there is no field of that type, returns 0.
         */
        int get_field_intensity( const tripoint &p, const field_type_id &type ) const;
        /**
         * Increment/decrement age of field entry at point.
         * @return resulting age or `-1_turns` if not present (does *not* create a new field).
         */
        time_duration mod_field_age( const tripoint &p, const field_type_id &type,
                                     const time_duration &offset );
        /**
         * Increment/decrement intensity of field entry at point, creating if not present,
         * removing if intensity becomes 0.
         * @return resulting intensity, or 0 for not present (either removed or not created at all).
         */
        int mod_field_intensity( const tripoint &p, const field_type_id &type, int offset );
        /**
         * Set age of field entry at point.
         * @param p Location of field
         * @param type ID of field
         * @param age New age of specified field
         * @param isoffset If true, the given age value is added to the existing value,
         * if false, the existing age is ignored and overridden.
         * @return resulting age or `-1_turns` if not present (does *not* create a new field).
         */
        time_duration set_field_age( const tripoint &p, const field_type_id &type,
                                     const time_duration &age, bool isoffset = false );
        /**
         * Set intensity of field entry at point, creating if not present,
         * removing if intensity becomes 0.
         * @param p Location of field
         * @param type ID of field
         * @param new_intensity New intensity of field
         * @param isoffset If true, the given new_intensity value is added to the existing value,
         * if false, the existing intensity is ignored and overridden.
         * @return resulting intensity, or 0 for not present (either removed or not created at all).
         */
        int set_field_intensity( const tripoint &p, const field_type_id &type, int new_intensity,
                                 bool isoffset = false );
        /**
         * @return true if there **might** be a field at `p`
         * @return false there's no fields at `p`
         */
        bool has_field_at( const tripoint &p, bool check_bounds = true );
        /**
         * Get field of specific type at point.
         * @return NULL if there is no such field entry at that place.
         */
        field_entry *get_field( const tripoint &p, const field_type_id &type );
        bool dangerous_field_at( const tripoint &p );
        /**
         * Add field entry at point, or set intensity if present
         * @return false if the field could not be created (out of bounds), otherwise true.
         */
        bool add_field( const tripoint &p, const field_type_id &type_id, int intensity = INT_MAX,
                        const time_duration &age = 0_turns, bool hit_player = true );
        /**
         * Remove field entry at xy, ignored if the field entry is not present.
         */
        void remove_field( const tripoint &p, const field_type_id &field_to_remove );

        // Splatters of various kind
        void add_splatter( const field_type_id &type, const tripoint &where, int intensity = 1 );
        void add_splatter_trail( const field_type_id &type, const tripoint &from, const tripoint &to );
        void add_splash( const field_type_id &type, const tripoint &center, int radius, int intensity );

        void propagate_field( const tripoint &center, const field_type_id &type,
                              int amount, int max_intensity = 0 );

        /**
         * Runs one cycle of emission @ref src which **may** result in propagation of fields
         * @param pos Location of emission
         * @param src Id of object producing the emission
         * @param mul Multiplies the chance and possibly qty (if `chance*mul > 100`) of the emission
         */
        void emit_field( const tripoint &pos, const emit_id &src, float mul = 1.0f );

        // Scent propagation helpers
        /**
         * Build the map of scent-resistant tiles.
         * Should be way faster than if done in `game.cpp` using public map functions.
         */
        void scent_blockers( std::array<std::array<char, MAPSIZE_X>, MAPSIZE_Y> &scent_transfer,
                             point min, point max );

        // Computers
        computer *computer_at( const tripoint &p );
        computer *add_computer( const tripoint &p, const std::string &name, int security );

        // Graffiti
        bool has_graffiti_at( const tripoint &p ) const;
        const std::string &graffiti_at( const tripoint &p ) const;
        void set_graffiti( const tripoint &p, const std::string &contents );
        void delete_graffiti( const tripoint &p );

        // Climbing
        /**
         * Checks 3x3 block centered on p for terrain to climb.
         * @return Difficulty of climbing check from point p.
         */
        int climb_difficulty( const tripoint &p ) const;

        // Support (of weight, structures etc.)
    private:
        // Tiles whose ability to support things was removed in the last turn
        std::set<tripoint> support_cache_dirty;
        // Checks if the tile is supported and adds it to support_cache_dirty if it isn't
        void support_dirty( const tripoint &p );
    public:

        // Returns true if terrain at p has NO flag TFLAG_NO_FLOOR,
        // if we're not in z-levels mode or if we're at lowest level
        bool has_floor( const tripoint &p ) const;

        /** Checks if there's a floor between the two tiles. They must be at most 1 tile away from each other in any dimension.
         *  If they're not at the same xy coord there must be floor on both of the relevant tiles
         */
        bool floor_between( const tripoint &first, const tripoint &second ) const;

        /** Does this tile support vehicles and furniture above it */
        bool supports_above( const tripoint &p ) const;
        bool has_floor_or_support( const tripoint &p ) const;

        /**
         * Handles map objects of given type (not creatures) falling down.
         * Returns true if anything changed.
         */
        /*@{*/
        void drop_everything( const tripoint &p );
        void drop_furniture( const tripoint &p );
        void drop_items( const tripoint &p );
        void drop_vehicle( const tripoint &p );
        void drop_fields( const tripoint &p );
        /*@}*/

        /**
         * Invoked @ref drop_everything on cached dirty tiles.
         */
        void process_falling();

        bool is_cornerfloor( const tripoint &p ) const;

        // mapgen.cpp functions
        void generate( const tripoint &p, const time_point &when );
        void place_spawns( const mongroup_id &group, int chance,
                           point p1, point p2, float density,
                           bool individual = false, bool friendly = false, const std::string &name = "NONE",
                           int mission_id = -1 );
        void place_gas_pump( const point &p, int charges, const itype_id &fuel_type );
        void place_gas_pump( const point &p, int charges );
        // 6 liters at 250 ml per charge
        void place_toilet( point p, int charges = 6 * 4 );
        void place_vending( point p, const item_group_id &type, bool reinforced = false );
        // places an NPC, if static NPCs are enabled or if force is true
        character_id place_npc( point p, const string_id<npc_template> &type,
                                bool force = false );
        void apply_faction_ownership( point p1, point p2, const faction_id &id );
        void add_spawn( const mtype_id &type, int count, const tripoint &p,
                        bool friendly = false, int faction_id = -1, int mission_id = -1,
                        const std::string &name = "NONE" ) const;
        void add_spawn( const mtype_id &type, int count, const tripoint &p,
                        spawn_disposition disposition, int faction_id = -1, int mission_id = -1,
                        const std::string &name = "NONE" ) const;
        void do_vehicle_caching( int z );
        // Note: in 3D mode, will actually build caches on ALL z-levels
        void build_map_cache( int zlev, bool skip_lightmap = false );
        // Unlike the other caches, this populates a supplied cache instead of an internal cache.
        void build_obstacle_cache( const tripoint &start, const tripoint &end,
                                   float( &obstacle_cache )[MAPSIZE_X][MAPSIZE_Y] );

        vehicle *add_vehicle( const vgroup_id &type, const tripoint &p, units::angle dir,
                              int init_veh_fuel = -1, int init_veh_status = -1,
                              bool merge_wrecks = true );
        vehicle *add_vehicle( const vgroup_id &type, point p, units::angle dir,
                              int init_veh_fuel = -1, int init_veh_status = -1,
                              bool merge_wrecks = true );
        vehicle *add_vehicle( const vproto_id &type, const tripoint &p, units::angle dir,
                              int init_veh_fuel = -1, int init_veh_status = -1,
                              bool merge_wrecks = true );
        vehicle *add_vehicle( const vproto_id &type, point p, units::angle dir,
                              int init_veh_fuel = -1, int init_veh_status = -1,
                              bool merge_wrecks = true );
        // Light/transparency
        float light_transparency( const tripoint &p ) const;
        // Assumes 0,0 is light map center
        lit_level light_at( const tripoint &p ) const;
        // Raw values for tilesets
        float ambient_light_at( const tripoint &p ) const;
        /**
         * Returns whether the tile at `p` is transparent(you can look past it).
         */
        bool is_transparent( const tripoint &p ) const;
        // End of light/transparency

        /**
         * Whether the player character (g->u) can see the given square (local map coordinates).
         * This only checks the transparency of the path to the target, the light level is not
         * checked.
         * @param t Target point to look at
         * @param max_range All squares that are further away than this are invisible.
         * Ignored if smaller than 0.
         */
        bool pl_sees( const tripoint &t, int max_range ) const;
        /**
         * Uses the map cache to tell if the player could see the given square.
         * pl_sees implies pl_line_of_sight
         * Used for infrared.
         */
        bool pl_line_of_sight( const tripoint &t, int max_range ) const;
        std::set<vehicle *> dirty_vehicle_list;

        /** return @ref abs_sub */
        tripoint get_abs_sub() const;
        /**
         * Translates local (to this map) coordinates of a square to global absolute coordinates.
         * Coordinates is in the system that is used by the ter/furn/i_at functions.
         * Output is in the same scale, but in global system.
         */
        tripoint getabs( const tripoint &p ) const;
        tripoint_abs_ms getglobal( const tripoint &p ) const;
        point getabs( point p ) const {
            return getabs( tripoint( p, abs_sub.z ) ).xy();
        }
        /**
         * Inverse of @ref getabs
         */
        tripoint getlocal( const tripoint &p ) const;
        tripoint getlocal( const tripoint_abs_ms &p ) const;
        point getlocal( point p ) const {
            return getlocal( tripoint( p, abs_sub.z ) ).xy();
        }
        virtual bool inbounds( const tripoint &p ) const;
        bool inbounds( const tripoint_abs_ms &p ) const;
        bool inbounds( point p ) const {
            return inbounds( tripoint( p, 0 ) );
        }

        bool inbounds_z( const int z ) const {
            return z >= -OVERMAP_DEPTH && z <= OVERMAP_HEIGHT;
        }

        /** Clips the coordinates of p to fit the map bounds */
        void clip_to_bounds( tripoint &p ) const;
        void clip_to_bounds( int &x, int &y ) const;
        void clip_to_bounds( int &x, int &y, int &z ) const;

        int getmapsize() const {
            return my_MAPSIZE;
        }
        bool has_zlevels() const {
            return zlevels;
        }

        // Not protected/private for mapgen_functions.cpp access
        // Rotates the current map 90*turns degrees clockwise
        // Useful for houses, shops, etc
        // @param turns number of 90 clockwise turns to make
        // @param setpos_safe if true, being used outside of mapgen and can use setpos to
        // set NPC positions.  if false, cannot use setpos
        void rotate( int turns, bool setpos_safe = false );

        // Monster spawning:
    public:
        /**
         * Spawn monsters from submap spawn points and from the overmap.
         * @param ignore_sight If true, monsters may spawn in the view of the player
         * character (useful when the whole map has been loaded instead, e.g.
         * when starting a new game, or after teleportation or after moving vertically).
         * If false, monsters are not spawned in view of player character.
         */
        void spawn_monsters( bool ignore_sight );

        /**
        * Checks to see if the corpse that is rotting away generates items when it does.
        * @param it item that is spawning creatures
        * @param pnt The point on this map where the item is and where bones/etc will be
        */
        void handle_decayed_corpse( const item &it, const tripoint &pnt );

        /**
        * Checks to see if the item that is rotting away generates a creature when it does.
        * @param item item that is spawning creatures
        * @param p The point on this map where the item is and creature will be
        */
        void rotten_item_spawn( const item &item, const tripoint &p );
    private:
        // Helper #1 - spawns monsters on one submap
        void spawn_monsters_submap( const tripoint &gp, bool ignore_sight );
        // Helper #2 - spawns monsters on one submap and from one group on this submap
        void spawn_monsters_submap_group( const tripoint &gp, mongroup &group, bool ignore_sight );

    protected:
        void saven( const tripoint &grid );
        void loadn( const tripoint &grid, bool update_vehicles );
        void loadn( point grid, bool update_vehicles ) {
            if( zlevels ) {
                for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
                    loadn( tripoint( grid, gridz ), update_vehicles );
                }

                // Note: we want it in a separate loop! It is a post-load cleanup
                // Since we're adding roofs, we want it to go up (from lowest to highest)
                for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; gridz++ ) {
                    add_roofs( tripoint( grid, gridz ) );
                }
            } else {
                loadn( tripoint( grid, abs_sub.z ), update_vehicles );
            }
        }
        /**
         * Fast forward a submap that has just been loading into this map.
         * This is used to rot and remove rotten items, grow plants, fill funnels etc.
         */
        void actualize( const tripoint &grid );
        /**
         * Hacks in missing roofs. Should be removed when 3D mapgen is done.
         */
        void add_roofs( const tripoint &grid );
        /**
         * Go through the list of items, update their rotten status and remove items
         * that have rotten away completely.
         * @param items items to remove
         * @param p The point on this map where the items are, used for rot calculation.
         * @param temperature flag that overrides temperature processing at certain locations
         */
        template <typename Container>
        void remove_rotten_items( Container &items, const tripoint &p, temperature_flag temperature );
        /**
         * Try to fill funnel based items here. Simulates rain from @p since till now.
         * @param p The location in this map where to fill funnels.
         */
        void fill_funnels( const tripoint &p, const time_point &since );
        /**
         * Try to grow a harvestable plant to the next stage(s).
         */
        void grow_plant( const tripoint &p );
        /**
         * Try to grow fruits on static plants (not planted by the player)
         * @param p Place to restock
         * @param time_since_last_actualize Time since this function has been
         * called the last time.
         */
        void restock_fruits( const tripoint &p, const time_duration &time_since_last_actualize );
        /**
         * Produce sap on tapped maple trees
         * @param p Location of tapped tree
         * @param time_since_last_actualize Time since this function has been
         * called the last time.
         */
        void produce_sap( const tripoint &p, const time_duration &time_since_last_actualize );
        /**
         * Radiation-related plant (and fungus?) death.
         */
        void rad_scorch( const tripoint &p, const time_duration &time_since_last_actualize );
        void decay_cosmetic_fields( const tripoint &p, const time_duration &time_since_last_actualize );

        void player_in_field( player &u );
        void monster_in_field( monster &z );
        /**
         * As part of the map shifting, this shifts the trap locations stored in @ref traplocs.
         * @param shift The amount shifting in submap, the same as go into @ref shift.
         */
        void shift_traps( const tripoint &shift );

        void copy_grid( const tripoint &to, const tripoint &from );
        void draw_map( mapgendata &dat );

        void draw_office_tower( const mapgendata &dat );
        void draw_lab( mapgendata &dat );
        void draw_temple( const mapgendata &dat );
        void draw_mine( mapgendata &dat );
        void draw_slimepit( mapgendata &dat );
        void draw_connections( const mapgendata &dat );

        // Builds a transparency cache and returns true if the cache was invalidated.
        // Used to determine if seen cache should be rebuilt.
        bool build_transparency_cache( int zlev );
        bool build_vision_transparency_cache( const Character &player );
        // fills lm with sunlight. pzlev is current player's zlevel
        void build_sunlight_cache( int pzlev );
    public:
        void build_outside_cache( int zlev );
        // Builds a floor cache and returns true if the cache was invalidated.
        // Used to determine if seen cache should be rebuilt.
        bool build_floor_cache( int zlev );
        // We want this visible in `game`, because we want it built earlier in the turn than the rest
        void build_floor_caches();
        // Checks all suspended tiles on a z level and adds those that are invalid to the support_dirty_cache */
        void update_suspension_cache( const int &z );
    protected:
        void generate_lightmap( int zlev );
        void build_seen_cache( const tripoint &origin, int target_z );
        void apply_character_light( Character &who );

        //Adds/removes player specific transparencies
        void apply_vision_transparency_cache( const tripoint &center, int target_z,
                                              float ( &vision_restore_cache )[9], bool ( &blocked_restore_cache )[8] );
        void restore_vision_transparency_cache( const tripoint &center, int target_z,
                                                float ( &vision_restore_cache )[9], bool ( &blocked_restore_cache )[8] );

        int my_MAPSIZE;
        bool zlevels;

        // stores vision adjustment for the tiles immediately surrounding the player, the order is given by eight_adjacent_offsets in point.h
        // examples of adjustment: crouching
        vision_adjustment vision_transparency_cache[8] = { VISION_ADJUST_NONE };

        /**
         * Absolute coordinates of first submap (get_submap_at(0,0))
         * This is in submap coordinates (see overmapbuffer for explanation).
         * It is set upon:
         * - loading submap at grid[0],
         * - generating submaps (@ref generate)
         * - shifting the map with @ref shift
         */
        tripoint abs_sub;
        /**
         * Sets @ref abs_sub, see there. Uses the same coordinate system as @ref abs_sub.
         */
        void set_abs_sub( const tripoint &p );

    private:
        field &get_field( const tripoint &p );

        /**
         * Get the submap pointer with given index in @ref grid, the index must be valid!
         */
        submap *getsubmap( size_t grididx ) const;
        /**
         * Get the submap pointer containing the specified position within the reality bubble.
         * (p) must be a valid coordinate, check with @ref inbounds.
         */
        submap *get_submap_at( const tripoint &p ) const;
        submap *get_submap_at( point p ) const {
            return get_submap_at( tripoint( p, abs_sub.z ) );
        }
        /**
         * Get the submap pointer containing the specified position within the reality bubble.
         * The same as other get_submap_at, (p) must be valid (@ref inbounds).
         * Also writes the position within the submap to offset_p
         */
        submap *get_submap_at( const tripoint &p, point &offset_p ) const;
        submap *get_submap_at( point p, point &offset_p ) const {
            return get_submap_at( { p, abs_sub.z }, offset_p );
        }
        /**
         * Get submap pointer in the grid at given grid coordinates. Grid coordinates must
         * be valid: 0 <= x < my_MAPSIZE, same for y.
         * z must be between -OVERMAP_DEPTH and OVERMAP_HEIGHT
         */
        submap *get_submap_at_grid( point gridp ) const {
            return getsubmap( get_nonant( gridp ) );
        }
        submap *get_submap_at_grid( const tripoint &gridp ) const;
    protected:
        /**
         * Get the index of a submap pointer in the grid given by grid coordinates. The grid
         * coordinates must be valid: 0 <= x < my_MAPSIZE, same for y.
         * Version with z-levels checks for z between -OVERMAP_DEPTH and OVERMAP_HEIGHT
         */
        size_t get_nonant( const tripoint &gridp ) const;
        size_t get_nonant( point gridp ) const {
            return get_nonant( { gridp, abs_sub.z } );
        }
        /**
         * Set the submap pointer in @ref grid at the give index. This is the inverse of
         * @ref getsubmap, any existing pointer is overwritten. The index must be valid.
         * The given submap pointer must not be null.
         */
        void setsubmap( size_t grididx, submap *smap );
    private:
        /** Caclulate the greatest populated zlevel in the loaded submaps and save in the level cache.
         * fills the map::max_populated_zlev and returns it
         * @return max_populated_zlev value
         */
        int calc_max_populated_zlev();
        /**
         * Conditionally invalidates max_pupulated_zlev cache if the submap uniformity change occurs above current
         *  max_pupulated_zlev value
         * @param zlev zlevel where uniformity change occured
         */
        void invalidate_max_populated_zlev( int zlev );

        /**
         * Internal version of the drawsq. Keeps a cached maptile for less re-getting.
         * Returns false if it has drawn all it should, true if `draw_from_above` should be called after.
         */
        bool draw_maptile( const catacurses::window &w, const tripoint &p,
                           const maptile &tile, const drawsq_params &params ) const;
        /**
         * Draws the tile as seen from above.
         */
        void draw_from_above( const catacurses::window &w, const tripoint &p,
                              const maptile &tile, const drawsq_params &params ) const;

        int determine_wall_corner( const tripoint &p ) const;
        // apply a circular light pattern immediately, however it's best to use...
        void apply_light_source( const tripoint &p, float luminance );
        // ...this, which will apply the light after at the end of generate_lightmap, and prevent redundant
        // light rays from causing massive slowdowns, if there's a huge amount of light.
        void add_light_source( const tripoint &p, float luminance );
        // Handle just cardinal directions and 45 deg angles.
        void apply_directional_light( const tripoint &p, int direction, float luminance );
        void apply_light_arc( const tripoint &p, units::angle, float luminance,
                              units::angle wideangle = 30_degrees );
        void apply_light_ray( bool lit[MAPSIZE_X][MAPSIZE_Y],
                              const tripoint &s, const tripoint &e, float luminance );
        void add_light_from_items( const tripoint &p, const item_stack::iterator &begin,
                                   const item_stack::iterator &end );
        std::unique_ptr<vehicle> add_vehicle_to_map( std::unique_ptr<vehicle> veh, bool merge_wrecks );

        // Internal methods used to bash just the selected features
        // "Externaled" for testing, because the interface to bashing is rng dependent
    public:
        // Information on what to bash/what was bashed is read from/written to the bash_params/bash_results struct
        bash_results bash_items( const tripoint &p, const bash_params &params );
        bash_results bash_field( const tripoint &p, const bash_params &params );

        // Successfully bashing things down
        bash_results bash_ter_success( const tripoint &p, const bash_params &params );
        bash_results bash_furn_success( const tripoint &p, const bash_params &params );

        // Gets the roof type of the tile at p
        // Second argument refers to whether we have to get a roof (we're over an unpassable tile)
        // or can just return air because we bashed down an entire floor tile
        ter_id get_roof( const tripoint &p, bool allow_air ) const;

        void process_items();
    private:
        // Iterates over every item on the map, passing each item to the provided function.
        void process_items_in_submap( submap &current_submap, const tripoint &gridp );
        void process_items_in_vehicles( submap &current_submap );
        void process_items_in_vehicle( vehicle &cur_veh, submap &current_submap );

        /** Enum used by functors in `function_over` to control execution. */
        enum iteration_state {
            ITER_CONTINUE = 0,  // Keep iterating
            ITER_SKIP_SUBMAP,   // Skip the rest of this submap
            ITER_SKIP_ZLEVEL,   // Skip the rest of this z-level
            ITER_FINISH         // End iteration
        };
        /**
        * Runs a functor over given submaps
        * over submaps in the area, getting next submap only when the current one "runs out" rather than every time.
        * gp in the functor is Grid (like `get_submap_at_grid`) coordinate of the submap,
        * Will silently clip the area to map bounds.
        * @param start Starting point for function
        * @param end End point for function
        * @param fun Function to run
        */
        /*@{*/
        template<typename Functor>
        void function_over( const tripoint &start, const tripoint &end, Functor fun ) const;
        /*@}*/

        /**
         * The list of currently loaded submaps. The size of this should not be changed.
         * After calling @ref load or @ref generate, it should only contain non-null pointers.
         * Use @ref getsubmap or @ref setsubmap to access it.
         */
        std::vector<submap *> grid;
        /**
         * This vector contains an entry for each trap type, it has therefor the same size
         * as the traplist vector. Each entry contains a list of all point on the map that
         * contain a trap of that type. The first entry however is always empty as it denotes the
         * tr_null trap.
         */
        std::vector< std::vector<tripoint> > traplocs;
        /**
         * Vector of tripoints containing active field-emitting furniture
         */
        std::vector<tripoint> field_furn_locs;
        /**
         * Holds caches for visibility, light, transparency and vehicles
         */
        std::array< std::unique_ptr<level_cache>, OVERMAP_LAYERS > caches;

        mutable std::array< std::unique_ptr<pathfinding_cache>, OVERMAP_LAYERS > pathfinding_caches;
        /**
         * Set of submaps that contain active items in absolute coordinates.
         */
        std::set<tripoint> submaps_with_active_items;

        /**
         * Cache of coordinate pairs recently checked for visibility.
         */
        mutable lru_cache<point, char> skew_vision_cache;

        /**
         * Vehicle list doesn't change often, but is pretty expensive.
         */
        VehicleList last_full_vehicle_list;
        bool last_full_vehicle_list_dirty = true;

        // Note: no bounds check
        level_cache &get_cache( int zlev ) const {
            return *caches[zlev + OVERMAP_DEPTH];
        }

        pathfinding_cache &get_pathfinding_cache( int zlev ) const;

        visibility_variables visibility_variables_cache;

        // caches the highest zlevel above which all zlevels are uniform
        // !value || value->first != map::abs_sub means cache is invalid
        std::optional<std::pair<tripoint, int>> max_populated_zlev = std::nullopt;

    public:
        const level_cache &get_cache_ref( int zlev ) const {
            return *caches[zlev + OVERMAP_DEPTH];
        }

        const pathfinding_cache &get_pathfinding_cache_ref( int zlev ) const;

        void update_pathfinding_cache( int zlev ) const;

        void update_visibility_cache( int zlev );
        const visibility_variables &get_visibility_variables_cache() const;

        void update_submap_active_item_status( const tripoint &p );

        // Just exposed for unit test introspection.
        const std::set<tripoint> &get_submaps_with_active_items() const {
            return submaps_with_active_items;
        }
        // Clips the area to map bounds
        tripoint_range<tripoint> points_in_rectangle(
            const tripoint &from, const tripoint &to ) const;
        tripoint_range<tripoint> points_in_radius(
            const tripoint &center, size_t radius, size_t radiusz = 0 ) const;
        /**
         * Yields a range of all points that are contained in the map and have the z-level of
         * this map (@ref abs_sub).
         */
        tripoint_range<tripoint> points_on_zlevel() const;
        /// Same as above, but uses the specific z-level. If the given z-level is invalid, it
        /// returns an empty range.
        tripoint_range<tripoint> points_on_zlevel( int z ) const;

        std::vector<item *> get_active_items_in_radius( const tripoint &center, int radius ) const;
        std::vector<item *> get_active_items_in_radius( const tripoint &center, int radius,
                special_item_type type ) const;

        /** returns positions of furnitures with matching flag in the overmap terrain*/
        std::vector<tripoint> find_furnitures_with_flag_in_omt( const tripoint &p,
                const std::string &flag );

        /**returns positions of furnitures with matching flag in the specified radius*/
        std::list<tripoint> find_furnitures_with_flag_in_radius( const tripoint &center, size_t radius,
                const std::string &flag,
                size_t radiusz = 0 );
        /**returns positions of furnitures or vehicle parts with matching flag in the specified radius*/
        std::list<tripoint> find_furnitures_or_vparts_with_flag_in_radius( const tripoint &center,
                size_t radius,
                const std::string &flag, size_t radiusz = 0 );
        /**returns creatures in specified radius*/
        std::list<Creature *> get_creatures_in_radius( const tripoint &center, size_t radius,
                size_t radiusz = 0 );

        level_cache &access_cache( int zlev );
        const level_cache &access_cache( int zlev ) const;
        bool dont_draw_lower_floor( const tripoint &p );
};

map &get_map();

template<int SIZE, int MULTIPLIER>
void shift_bitset_cache( std::bitset<SIZE *SIZE> &cache, point s );

bool ter_furn_has_flag( const ter_t &ter, const furn_t &furn, ter_bitflags flag );
class tinymap : public map
{
        friend class editmap;
    public:
        tinymap( int mapsize = 2, bool zlevels = false );
        bool inbounds( const tripoint &p ) const override;
};

class fake_map : public tinymap
{
    private:
        std::vector<std::unique_ptr<submap>> temp_submaps_;
    public:
        fake_map( const furn_id &fur_type, const ter_id &ter_type, const trap_id &trap_type,
                  int fake_map_z );
        ~fake_map() override;
};

