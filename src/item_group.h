#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <set>

#include "item.h"
#include "type_id.h"

struct itype;

class JsonObject;
class JsonValue;
class time_point;

namespace item_group
{
/**
 * Returns a random item from the item group, handles packaged food by putting it into its
 * container and returning the container item.
 * Note that this may return a null-item, if the group does not exist, is empty or did not
 * create an item this time. You have to check this with @ref item::is_null.
 */
detached_ptr<item> item_from( const item_group_id &group_id, const time_point &birthday );
/**
 * Same as above but with implicit birthday at turn 0.
 */
detached_ptr<item> item_from( const item_group_id &group_id );

using ItemList = std::vector<item *>;
/**
 * Create items from the given group. It creates as many items as the group definition requests.
 * For example if the group is a distribution that only contains item ids it will create
 * a single item.
 * If the group is a collection with several entries it can contain more than one item (or none
 * at all).
 * This function also creates ammo for guns, if this is requested in the item group and puts
 * stuff into containers if that is required.
 * The returned list may contain any number of items (or be empty), but it will never
 * contain null-items.
 * @param group_id The identifier of the item group. You may check its validity
 * with @ref group_is_defined.
 * @param birthday The birthday (@ref item::bday) of the items created by this function.
 */
std::vector<detached_ptr<item>> items_from( const item_group_id &group_id,
                             const time_point &birthday );

/**
 * Same as above but with implicit birthday at turn 0.
 */
std::vector<detached_ptr<item>> items_from( const item_group_id &group_id );
/**
 * Check whether a specific item group contains a specific item type.
 */
bool group_contains_item( const item_group_id &group_id, const itype_id &type_id );
/**
 * Return every item type that can possibly be spawned by the item group
 */
std::set<const itype *> every_possible_item_from( const item_group_id &group_id );
/**
 * Check whether an item group of the given id exists. You may use this to either choose an
 * alternative group or check the json definitions for consistency (spawn data in json that
 * refers to a non-existing group is broken), or just alert the player.
 */
bool group_is_defined( const item_group_id &group_id );
/**
 * Shows an menu to debug the item groups.
 */
void debug_spawn();
/**
 * See @ref Item_factory::load_item_group
 */
void load_item_group( const JsonObject &jsobj, const item_group_id &group_id,
                      const std::string &subtype );
/**
 * Get an item group id and (optionally) load an inlined item group.
 *
 * If the value is string, it's assumed to be an item group id and it's
 * returned directly.
 *
 * If the value is a JSON object, it is loaded as item group. The group will be given a
 * unique id (if the JSON object contains an id, it is ignored) and that id will be returned.
 * If the JSON object does not contain a subtype, the given default is used.
 *
 * If the value is a JSON array, it is loaded as item group: the default_subtype will be
 * used as subtype of the new item group and the array is loaded like the "entries" array of
 * a item group definition (see format of item groups).
 *
 * @param stream Stream to load from
 * @param default_subtype If an inlined item group is loaded this is used as the default
 * subtype. It must be either "distribution" or "collection". See @ref Item_group.
 * @throw JsonError as usual for JSON errors, including invalid input values.
 */
item_group_id load_item_group( const JsonValue &value, const std::string &default_subtype );
} // namespace item_group

/**
 * Base interface for item spawn.
 * Used to generate a list of items.
 */
class Item_spawn_data
{
    public:
        using ItemList = std::vector<item *>;
        using RecursionList = std::vector<std::string>;

        Item_spawn_data( int _probability ) : probability( _probability ) { }
        virtual ~Item_spawn_data() = default;
        /**
         * Create a list of items. The create list might be empty.
         * No item of it will be the null item.
         * @param[in] birthday All items have that value as birthday.
         * @param[out] rec Recursion list, output goes here
         */
        virtual std::vector<detached_ptr<item>> create( const time_point &birthday,
                                             RecursionList &rec ) const = 0;
        std::vector<detached_ptr<item>> create( const time_point &birthday ) const;
        /**
         * The same as create, but create a single item only.
         * The returned item might be a null item!
         */
        virtual detached_ptr<item> create_single( const time_point &birthday,
                RecursionList &rec ) const = 0;
        detached_ptr<item> create_single( const time_point &birthday ) const;
        /**
         * Check item / spawn settings for consistency. Includes
         * checking for valid item types and valid settings.
         */
        virtual void check_consistency( const std::string &context ) const = 0;
        /**
         * For item blacklisted, remove the given item from this and
         * all linked groups.
         */
        virtual bool remove_item( const itype_id &itemid ) = 0;
        virtual bool replace_item( const itype_id &itemid, const itype_id &replacementid ) = 0;
        virtual bool has_item( const itype_id &itemid ) const = 0;

        virtual std::set<const itype *> every_item() const = 0;

        /** probability, used by the parent object. */
        int probability;
};

using ItemFn = std::function < detached_ptr<item> ( detached_ptr<item> &&it ) >;

/**
 * Creates a single item, but can change various aspects
 * of the created item.
 * This is basically a wrapper around @ref Single_item_creator
 * that adds item properties.
 */
class Item_modifier
{
    public:
        std::pair<int, int> damage;
        std::pair<int, int> count;
        /**
         * Charges to spawn the item with, if this turns out to
         * be negative, the default charges are used.
         */
        std::pair<int, int> dirt;
        std::pair<int, int> charges;
        /**
         * Ammo for guns. If NULL the gun spawns without ammo.
         * This takes @ref charges and @ref with_ammo into account.
         */
        std::unique_ptr<Item_spawn_data> ammo;
        /**
         * Item should spawn inside this container, can be NULL,
         * if item should not spawn in a container.
         * If the created item is a liquid and it uses the default
         * charges, it will expand/shrink to fill the container completely.
         * If it is created with to much charges, they are reduced.
         * If it is created with the non-default charges, but it still fits
         * it is not changed.
         */
        std::unique_ptr<Item_spawn_data> container;
        /**
         * This is used to create the contents of an item.
         */
        std::unique_ptr<Item_spawn_data> contents;

        /**
         * Custom flags to be added to the item.
         */
        std::vector<flag_id> custom_flags;

        /**
         * Custom functions to be applied to the item after its creation.
         */
        std::vector<ItemFn> postprocess_fns;

        Item_modifier();
        Item_modifier( Item_modifier && ) = default;

        detached_ptr<item> modify( detached_ptr<item> &&new_item ) const;
        void check_consistency( const std::string &context ) const;
        bool remove_item( const itype_id &itemid );
        bool replace_item( const itype_id &itemid, const itype_id &replacementid );

        // Currently these always have the same chance as the item group it's part of, but
        // theoretically it could be defined per-item / per-group.
        /** Chance [0-100%] for items to spawn with ammo (plus default magazine if necessary) */
        int with_ammo;
        /** Chance [0-100%] for items to spawn with their default magazine (if any) */
        int with_magazine;
};
/**
 * Basic item creator. It contains either the item id directly,
 * or a name of a group that it queries for it.
 * It can spawn a single item and a item list.
 * Creates a single item, with its standard properties
 * (charges, no damage, ...).
 * As it inherits from @ref Item_spawn_data it can also wrap that
 * created item into a list. That list will always contain a
 * single item (or no item at all).
 * Note that the return single item my be a null item.
 * The returned item list will (according to the definition in
 * @ref Item_spawn_data) never contain null items, that's why it
 * might be empty.
 */
class Single_item_creator : public Item_spawn_data
{
    public:
        enum Type {
            S_ITEM_GROUP,
            S_ITEM,
            S_NONE,
        };

        Single_item_creator( const std::string &id, Type type, int probability );
        ~Single_item_creator() override = default;

        /**
         * Id of the item group or id of the item.
         */
        std::string id;
        Type type;
        std::optional<Item_modifier> modifier;

        void inherit_ammo_mag_chances( int ammo, int mag );

        std::vector<detached_ptr<item>> create( const time_point &birthday,
                                                RecursionList &rec ) const override;
        detached_ptr<item>create_single( const time_point &birthday, RecursionList &rec ) const override;
        void check_consistency( const std::string &context ) const override;
        bool remove_item( const itype_id &itemid ) override;
        bool replace_item( const itype_id &itemid, const itype_id &replacementid ) override;

        bool has_item( const itype_id &itemid ) const override;
        std::set<const itype *> every_item() const override;
};

/**
 * This is a list of item spawns. It can act as distribution
 * (one entry from the list) or as collection (all entries get a chance).
 */
class Item_group : public Item_spawn_data
{
    public:
        using ItemList = std::vector<item *>;
        enum Type {
            G_COLLECTION,
            G_DISTRIBUTION
        };

        Item_group( Type type, int probability, int ammo_chance, int magazine_chance );
        ~Item_group() override = default;

        const Type type;
        /**
         * Item data to create item from and probability
         * that items should be created.
         * If type is G_COLLECTION, probability is in percent.
         * A value of 100 means always create items, a value of 0 never.
         * If type is G_DISTRIBUTION, probability is relative,
         * the sum probability is sum_prob.
         */
        using prop_list = std::vector<std::unique_ptr<Item_spawn_data> >;

        void add_item_entry( const itype_id &itemid, int probability );
        void add_group_entry( const item_group_id &groupid, int probability );
        /**
         * Once the relevant data has been read from JSON, this function is always called (either from
         * @ref Item_factory::add_entry, @ref add_item_entry or @ref add_group_entry). Its purpose is to add
         * a Single_item_creator or Item_group to @ref items.
         */
        void add_entry( std::unique_ptr<Item_spawn_data> ptr );

        std::vector<detached_ptr<item>> create( const time_point &birthday,
                                                RecursionList &rec ) const override;
        detached_ptr<item> create_single( const time_point &birthday, RecursionList &rec ) const override;
        void check_consistency( const std::string &context ) const override;
        bool remove_item( const itype_id &itemid ) override;
        bool replace_item( const itype_id &itemid, const itype_id &replacementid ) override;
        bool has_item( const itype_id &itemid ) const override;
        std::set<const itype *> every_item() const override;
        /**
         * Hack for testing. TODO: Find a better way.
         */
        const prop_list &get_items() const {
            return items;
        }

        /**
         * These aren't directly used. Instead, the values (both with a default value of 0) "trickle down"
         * to apply to every item/group entry within this item group. It's added to the
         * @ref Single_item_creator's @ref Item_modifier via @ref Single_item_creator::inherit_ammo_mag_chances()
         */
        /** Every item in this group has this chance [0-100%] for items to spawn with ammo (plus default magazine if necessary) */
        const int with_ammo;
        /** Every item in this group has this chance [0-100%] for items to spawn with their default magazine (if any) */
        const int with_magazine;

    protected:
        /**
         * Contains the sum of the probability of all entries
         * that this group contains.
         */
        int sum_prob;
        /**
         * Links to the entries in this group.
         */
        prop_list items;
};


