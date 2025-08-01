#include "mondeath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "creature.h"
#include "enums.h"
#include "explosion_queue.h"
#include "field_type.h"
#include "fungal_effects.h"
#include "game.h"
#include "harvest.h"
#include "item.h"
#include "item_group.h"
#include "item_stack.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "kill_tracker.h"
#include "line.h"
#include "make_static.h"
#include "map.h"
#include "map_iterator.h"
#include "mattack_actors.h"
#include "mattack_common.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "player.h"
#include "pldata.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "timed_event.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "weighted_list.h"

static const efftype_id effect_ai_controlled( "ai_controlled" );
static const efftype_id effect_amigara( "amigara" );
static const efftype_id effect_boomered( "boomered" );
static const efftype_id effect_darkness( "darkness" );
static const efftype_id effect_glowing( "glowing" );
static const efftype_id effect_no_ammo( "no_ammo" );
static const efftype_id effect_pacified( "pacified" );
static const efftype_id effect_rat( "rat" );

static const itype_id itype_processor( "processor" );

static const species_id species_BLOB( "BLOB" );
static const species_id ZOMBIE( "ZOMBIE" );

static const mtype_id mon_blob( "mon_blob" );
static const mtype_id mon_blob_brain( "mon_blob_brain" );
static const mtype_id mon_blob_small( "mon_blob_small" );
static const mtype_id mon_breather( "mon_breather" );
static const mtype_id mon_breather_hub( "mon_breather_hub" );
static const mtype_id mon_creeper_hub( "mon_creeper_hub" );
static const mtype_id mon_creeper_vine( "mon_creeper_vine" );
static const mtype_id mon_giant_cockroach_nymph( "mon_giant_cockroach_nymph" );
static const mtype_id mon_halfworm( "mon_halfworm" );
static const mtype_id mon_sewer_rat( "mon_sewer_rat" );
static const mtype_id mon_thing( "mon_thing" );
static const mtype_id mon_zombie_dancer( "mon_zombie_dancer" );
static const mtype_id mon_zombie_hulk( "mon_zombie_hulk" );

static const trait_id trait_KILLER( "KILLER" );
static const trait_id trait_PACIFIST( "PACIFIST" );
static const trait_id trait_PSYCHOPATH( "PSYCHOPATH" );

static const trait_flag_str_id trait_flag_PRED1( "PRED1" );
static const trait_flag_str_id trait_flag_PRED2( "PRED2" );
static const trait_flag_str_id trait_flag_PRED3( "PRED3" );
static const trait_flag_str_id trait_flag_PRED4( "PRED4" );

void mdeath::normal( monster &z )
{
    if( z.no_corpse_quiet ) {
        return;
    }

    if( z.type->in_species( ZOMBIE ) ) {
        sfx::play_variant_sound( "mon_death", "zombie_death", sfx::get_heard_volume( z.pos() ) );
    }

    if( g->u.sees( z ) ) {
        //Currently it is possible to get multiple messages that a monster died.
        add_msg( m_good, _( "The %s dies!" ), z.name() );
    }

    const int max_hp = std::max( z.get_hp_max(), 1 );
    const float overflow_damage = std::max( -z.get_hp(), 0 );
    const float corpse_damage = 2.5 * overflow_damage / max_hp;
    const bool pulverized = corpse_damage > 5 && overflow_damage > z.get_hp_max();

    z.bleed(); // leave some blood if we have to

    if( !pulverized ) {
        make_mon_corpse( z, static_cast<int>( std::floor( corpse_damage * itype::damage_scale ) ) );
    }
    // if mdeath::splatter was set along normal makes sure it is not called twice
    bool splatt = false;
    for( const auto &deathfunction : z.type->dies ) {
        if( deathfunction == mdeath::splatter ) {
            splatt = true;
        }
    }
    if( !splatt ) {
        splatter( z );
    }
}

static void scatter_chunks( const itype_id &chunk_name, int chunk_amt, monster &z, int distance,
                            int pile_size = 1 )
{
    // can't have less than one item in a pile or it would cause an infinite loop
    pile_size = std::max( pile_size, 1 );
    // can't have more items in a pile than total items
    pile_size = std::min( chunk_amt, pile_size );
    distance = std::abs( distance );
    map &here = get_map();
    for( int i = 0; i < chunk_amt; i += pile_size ) {
        bool drop_chunks = true;
        tripoint tarp( z.pos() + point( rng( -distance, distance ), rng( -distance, distance ) ) );
        const auto traj = line_to( z.pos(), tarp );
        tripoint prev_point = z.pos();
        for( size_t j = 0; j < traj.size(); j++ ) {
            tarp = traj[j];

            bool obstructed = false;
            if( here.obstructed_by_vehicle_rotation( prev_point, tarp ) ) {
                if( one_in( 2 ) ) {
                    tarp.x = prev_point.x;
                } else {
                    tarp.y = prev_point.y;
                }
                obstructed = true;
            }

            if( one_in( 2 ) && z.bloodType() ) {
                here.add_splatter( z.bloodType(), tarp );
            } else if( z.gibType() ) {
                here.add_splatter( z.gibType(), tarp, rng( 1, j + 1 ) );
            }


            if( here.impassable( tarp ) ) {
                here.bash( tarp, distance );
                if( here.impassable( tarp ) ) {
                    // Target is obstacle, not destroyed by bashing,
                    // stop trajectory in front of it, if this is the first
                    // point (e.g. wall adjacent to monster), don't drop anything on it
                    if( j > 0 ) {
                        tarp = traj[j - 1];
                    } else {
                        drop_chunks = false;
                    }
                    break;
                }
            }
            //Don't lower j until after it's used in bashing
            if( obstructed ) {
                j--;
            }
            prev_point = tarp;
        }
        if( drop_chunks ) {
            here.add_item_or_charges( tarp, item::spawn( chunk_name, calendar::turn, pile_size ) );
        }
    }
}

void mdeath::splatter( monster &z )
{
    map &here = get_map();
    const bool gibbable = !z.type->has_flag( MF_NOGIB );

    const int max_hp = std::max( z.get_hp_max(), 1 );
    const float overflow_damage = std::max( -z.get_hp(), 0 );
    const float corpse_damage = 2.5 * overflow_damage / max_hp;
    bool pulverized = corpse_damage > 5 && overflow_damage > z.get_hp_max();
    // make sure that full splatter happens when this is a set death function, not part of normal
    for( const auto &deathfunction : z.type->dies ) {
        if( deathfunction == mdeath::splatter ) {
            pulverized = true;
        }
    }

    const field_type_id type_blood = z.bloodType();
    const field_type_id type_gib = z.gibType();

    if( gibbable ) {
        const auto area = here.points_in_radius( z.pos(), 1 );
        int number_of_gibs = std::min( std::floor( corpse_damage ) - 1, 1 + max_hp / 5.0f );

        if( pulverized && z.type->size >= creature_size::medium ) {
            number_of_gibs += rng( 1, 6 );
            sfx::play_variant_sound( "mon_death", "zombie_gibbed", sfx::get_heard_volume( z.pos() ) );
        }

        for( int i = 0; i < number_of_gibs; ++i ) {
            if( type_blood ) {
                here.add_splatter( type_gib, random_entry( area ), rng( 1, i + 1 ) );
            }
            if( type_gib ) {
                here.add_splatter( type_gib, random_entry( area ) );
            }
        }
    }
    // 1% of the weight of the monster is the base, with overflow damage as a multiplier
    int gibbed_weight = rng( 0, std::round( to_gram( z.get_weight() ) / 100.0 *
                                            ( overflow_damage / max_hp + 1 ) ) );
    const uint64_t z_weight = to_gram( z.get_weight() );
    // limit gibbing to 15%
    gibbed_weight = std::min( static_cast<uint64_t>( gibbed_weight ), z_weight * 15 / 100 );

    if( pulverized && gibbable ) {
        float overflow_ratio = overflow_damage / max_hp + 1;
        int gib_distance = std::round( rng( 2, 4 ) );
        for( const auto &entry : *z.type->harvest ) {
            // only flesh and bones survive.
            if( entry.type == "flesh" || entry.type == "bone" ) {
                // the larger the overflow damage, the less you get
                itype_id item_id( entry.drop );
                const int chunk_amt = entry.mass_ratio / overflow_ratio / 10 * z_weight /
                                      to_gram( item_id->weight );
                scatter_chunks( item_id, chunk_amt, z, gib_distance,
                                chunk_amt / ( gib_distance - 1 ) );
                gibbed_weight -= entry.mass_ratio / overflow_ratio / 20 * z_weight;
            }
        }
        // add corpse with gib flag
        detached_ptr<item> corpse = item::make_corpse( z.type->id, calendar::turn, z.unique_name,
                                    z.get_upgrade_time() );
        // Set corpse to damage that aligns with being pulped
        corpse->set_damage( 4000 );
        corpse->set_flag( STATIC( flag_id( "GIBBED" ) ) );
        if( z.has_effect( effect_no_ammo ) ) {
            corpse->set_var( "no_ammo", "no_ammo" );
        }
        here.add_item_or_charges( z.pos(), std::move( corpse ) );
    }
}

void mdeath::acid( monster &z )
{
    if( g->u.sees( z ) ) {
        if( z.type->dies.size() ==
            1 ) { //If this death function is the only function. The corpse gets dissolved.
            add_msg( m_mixed, _( "The %s's body dissolves into acid." ), z.name() );
        } else {
            add_msg( m_warning, _( "The %s's body leaks acid." ), z.name() );
        }
    }
    g->m.add_field( z.pos(), fd_acid, 3 );
}

void mdeath::boomer( monster &z )
{
    std::string explode = string_format( _( "a %s explode!" ), z.name() );
    sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "small" );
    for( const tripoint &dest : g->m.points_in_radius( z.pos(), 1 ) ) { // *NOPAD*
        g->m.bash( dest, 10 );
        if( monster *const target = g->critter_at<monster>( dest ) ) {
            target->stumble();
            target->moves -= 250;
        }
    }

    if( rl_dist( z.pos(), g->u.pos() ) == 1 ) {
        g->u.add_env_effect( effect_boomered, body_part_eyes, 2, 24_turns );
    }

    g->m.propagate_field( z.pos(), fd_bile, 15, 1 );
}

void mdeath::boomer_glow( monster &z )
{
    std::string explode = string_format( _( "a %s explode!" ), z.name() );
    sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "small" );

    for( const tripoint &dest : g->m.points_in_radius( z.pos(), 1 ) ) { // *NOPAD*
        g->m.bash( dest, 10 );
        if( monster *const target = g->critter_at<monster>( dest ) ) {
            target->stumble();
            target->moves -= 250;
        }
        if( Creature *const critter = g->critter_at( dest ) ) {
            critter->add_env_effect( effect_boomered, body_part_eyes, 5, 25_turns );
            for( int i = 0; i < rng( 2, 4 ); i++ ) {
                const bodypart_str_id &bp = random_body_part();
                critter->add_env_effect( effect_glowing, bp, 4, 4_minutes );
                if( critter->has_effect( effect_glowing ) ) {
                    break;
                }
            }
        }
    }

    g->m.propagate_field( z.pos(), fd_bile, 30, 2 );
}

void mdeath::kill_vines( monster &z )
{
    const std::vector<Creature *> vines = g->get_creatures_if( [&]( const Creature & critter ) {
        const monster *const mon = dynamic_cast<const monster *>( &critter );
        return mon && mon->type->id == mon_creeper_vine;
    } );
    const std::vector<Creature *> hubs = g->get_creatures_if( [&]( const Creature & critter ) {
        const monster *const mon = dynamic_cast<const monster *>( &critter );
        return mon && mon != &z && mon->type->id == mon_creeper_hub;
    } );

    for( Creature *const vine : vines ) {
        int dist = rl_dist( vine->pos(), z.pos() );
        bool closer = false;
        for( auto &j : hubs ) {
            if( rl_dist( vine->pos(), j->pos() ) < dist ) {
                break;
            }
        }
        if( !closer ) { // TODO: closer variable is not being updated and is always false!
            vine->die( &z );
        }
    }
}

void mdeath::vine_cut( monster &z )
{
    std::vector<monster *> vines;
    for( const tripoint &tmp : g->m.points_in_radius( z.pos(), 1 ) ) {
        if( tmp == z.pos() ) {
            continue; // Skip ourselves
        }
        if( monster *const neighbor = g->critter_at<monster>( tmp ) ) {
            if( neighbor->type->id == mon_creeper_vine ) {
                vines.push_back( neighbor );
            }
        }
    }

    for( auto &vine : vines ) {
        bool found_neighbor = false;
        for( const tripoint &dest : g->m.points_in_radius( vine->pos(), 1 ) ) {
            if( dest != z.pos() ) {
                // Not the dying vine
                if( monster *const v = g->critter_at<monster>( dest ) ) {
                    if( v->type->id == mon_creeper_hub || v->type->id == mon_creeper_vine ) {
                        found_neighbor = true;
                        break;
                    }
                }
            }
        }
        if( !found_neighbor ) {
            vine->die( &z );
        }
    }
}

void mdeath::triffid_heart( monster &z )
{
    if( g->u.sees( z ) ) {
        add_msg( m_warning, _( "The surrounding roots begin to crack and crumble." ) );
    }
    g->timed_events.add( TIMED_EVENT_ROOTS_DIE, calendar::turn + 10_minutes );
}

void mdeath::fungus( monster &z )
{
    //~ the sound of a fungus dying
    sounds::sound( z.pos(), 10, sounds::sound_t::combat, _( "Pouf!" ), false, "misc", "puff" );

    fungal_effects fe( *g, g->m );
    for( const tripoint &sporep : g->m.points_in_radius( z.pos(), 1 ) ) { // *NOPAD*
        if( g->m.impassable( sporep ) && !get_map().obstructed_by_vehicle_rotation( z.pos(), sporep ) ) {
            continue;
        }
        // z is dead, don't credit it with the kill
        // Maybe credit z's killer?
        fe.fungalize( sporep, nullptr, fungal_opt.spore_chance );
    }
}

void mdeath::disintegrate( monster &z )
{
    if( g->u.sees( z ) ) {
        add_msg( m_good, _( "The %s disintegrates!" ), z.name() );
    }
}

void mdeath::worm( monster &z )
{
    if( g->u.sees( z ) ) {
        if( z.type->dies.size() == 1 ) {
            add_msg( m_good, _( "The %s splits in two!" ), z.name() );
        } else {
            add_msg( m_warning, _( "Two worms crawl out of the %s's corpse." ), z.name() );
        }
    }

    int worms = 2;
    while( worms > 0 && g->place_critter_around( mon_halfworm, z.pos(), 1 ) ) {
        worms--;
    }
}

void mdeath::disappear( monster &z )
{
    if( g->u.sees( z ) ) {
        add_msg( m_good, _( "The %s disappears." ), z.name() );
    }
}

void mdeath::guilt( monster &z )
{
    const int MAX_GUILT_DISTANCE = 5;
    int kill_count = g->get_kill_tracker().kill_count( z.type->id );
    int maxKills = 100; // this is when the player stop caring altogether.

    // different message as we kill more of the same monster
    std::string msg = _( "You feel guilty for killing %s." ); // default guilt message
    game_message_type msgtype = m_bad; // default guilt message type
    std::map<int, std::string> guilt_tresholds;
    guilt_tresholds[75] = _( "You feel ashamed for killing %s." );
    guilt_tresholds[50] = _( "You regret killing %s." );
    guilt_tresholds[25] = _( "You feel remorse for killing %s." );

    if( g->u.has_trait( trait_PSYCHOPATH ) || g->u.has_trait_flag( trait_flag_PRED3 ) ||
        g->u.has_trait_flag( trait_flag_PRED4 ) || g->u.has_trait( trait_KILLER ) ) {
        return;
    }
    if( rl_dist( z.pos(), g->u.pos() ) > MAX_GUILT_DISTANCE ) {
        // Too far away, we can deal with it.
        return;
    }
    if( z.get_hp() >= 0 ) {
        // We probably didn't kill it
        return;
    }
    if( kill_count >= maxKills ) {
        // player no longer cares
        if( kill_count == maxKills ) {
            //~ Message after killing a lot of monsters which would normally affect the morale negatively. %s is the monster name, it will be pluralized with a number of 100.
            add_msg( m_good, _( "After killing so many bloody %s you no longer care "
                                "about their deaths anymore." ), z.name( maxKills ) );
        }
        return;

    } else if( ( g->u.has_trait_flag( trait_flag_PRED1 ) ) ||
               ( g->u.has_trait_flag( trait_flag_PRED1 ) ) ) {
        msg = ( _( "Culling the weak is distasteful, but necessary." ) );
        msgtype = m_neutral;
    } else {
        msgtype = m_bad;
        for( auto &guilt_treshold : guilt_tresholds ) {
            if( kill_count >= guilt_treshold.first ) {
                msg = guilt_treshold.second;
                break;
            }
        }
    }

    add_msg( msgtype, msg, z.name() );

    int moraleMalus = -50 * ( 1.0 - ( static_cast<float>( kill_count ) / maxKills ) );
    int maxMalus = -250 * ( 1.0 - ( static_cast<float>( kill_count ) / maxKills ) );
    time_duration duration = 30_minutes * ( 1.0 - ( static_cast<float>( kill_count ) / maxKills ) );
    time_duration decayDelay = 3_minutes * ( 1.0 - ( static_cast<float>( kill_count ) / maxKills ) );
    if( z.type->in_species( ZOMBIE ) ) {
        moraleMalus /= 10;
        if( g->u.has_trait( trait_PACIFIST ) ) {
            moraleMalus *= 5;
        } else if( g->u.has_trait_flag( trait_flag_PRED1 ) ) {
            moraleMalus /= 4;
        } else if( g->u.has_trait_flag( trait_flag_PRED2 ) ) {
            moraleMalus /= 5;
        }
    }
    g->u.add_morale( MORALE_KILLED_MONSTER, moraleMalus, maxMalus, duration, decayDelay );

}

void mdeath::blobsplit( monster &z )
{
    int speed = z.get_speed() - rng( 30, 50 );
    g->m.spawn_item( z.pos(), "slime_scrap", 1, 0, calendar::turn );
    if( z.get_speed() <= 0 ) {
        if( g->u.sees( z ) ) {
            // TODO: Add vermin-tagged tiny versions of the splattered blob  :)
            add_msg( m_good, _( "The %s splatters apart." ), z.name() );
        }
        return;
    }
    if( g->u.sees( z ) ) {
        if( z.type->dies.size() == 1 ) {
            add_msg( m_good, _( "The %s splits in two!" ), z.name() );
        } else {
            add_msg( m_bad, _( "Two small blobs slither out of the corpse." ) );
        }
    }

    const mtype_id &child = speed < 50 ? mon_blob_small : mon_blob;
    for( int s = 0; s < 2; s++ ) {
        if( monster *const blob = g->place_critter_around( child, z.pos(), 1 ) ) {
            blob->make_ally( z );
            blob->set_speed_base( speed );
            blob->set_hp( speed );
        }
    }
}

void mdeath::brainblob( monster &z )
{
    for( monster &critter : g->all_monsters() ) {
        if( critter.type->in_species( species_BLOB ) && critter.type->id != mon_blob_brain ) {
            critter.remove_effect( effect_ai_controlled );
        }
    }
    blobsplit( z );
}

void mdeath::jackson( monster &z )
{
    for( monster &critter : g->all_monsters() ) {
        if( critter.type->id == mon_zombie_dancer ) {
            critter.poly( mon_zombie_hulk );
            critter.remove_effect( effect_ai_controlled );
        }
        if( g->u.sees( z ) ) {
            add_msg( m_warning, _( "The music stops!" ) );
        }
    }
}

void mdeath::melt( monster &z )
{
    if( g->u.sees( z ) ) {
        add_msg( m_good, _( "The %s melts away." ), z.name() );
    }
}

void mdeath::amigara( monster &z )
{
    const bool has_others = g->get_creature_if( [&]( const Creature & critter ) {
        if( const monster *const candidate = dynamic_cast<const monster *>( &critter ) ) {
            return candidate->type == z.type;
        }
        return false;
    } );
    if( has_others ) {
        return;
    }

    // We were the last!
    if( g->u.has_effect( effect_amigara ) ) {
        g->u.remove_effect( effect_amigara );
        add_msg( _( "Your obsession with the fault fades away…" ) );
    }

    g->m.spawn_artifact( z.pos() );
}

void mdeath::thing( monster &z )
{
    g->place_critter_at( mon_thing, z.pos() );
}

void mdeath::explode( monster &z )
{
    int size = 0;
    switch( z.type->size ) {
        case creature_size::tiny:
            size = 4;
            break;
        case creature_size::small:
            size = 8;
            break;
        case creature_size::medium:
            size = 14;
            break;
        case creature_size::large:
            size = 20;
            break;
        case creature_size::huge:
            size = 26;
            break;
        default:
            size = 15;
            break;
    }
    explosion_handler::explosion( z.pos(), &z, size );
    explosion_handler::get_explosion_queue().execute();
}

void mdeath::focused_beam( monster &z )
{
    map_stack items = g->m.i_at( z.pos() );
    for( map_stack::iterator it = items.begin(); it != items.end(); ) {
        if( ( *it )->typeId() == itype_processor ) {
            it = items.erase( it );
        } else {
            ++it;
        }
    }

    if( !z.get_items().empty() ) {

        if( g->u.sees( z ) ) {
            add_msg( m_warning, _( "As the final light is destroyed, it erupts in a blinding flare!" ) );
        }

        item &settings = *z.get_items()[0];

        point p2( z.posx() + settings.get_var( "SL_SPOT_X", 0 ), z.posy() + settings.get_var( "SL_SPOT_Y",
                  0 ) );
        tripoint p( p2, z.posz() );

        std::vector <tripoint> traj = line_to( z.pos(), p, 0, 0 );
        tripoint last_point = z.pos();
        for( auto &elem : traj ) {
            if( !g->m.is_transparent( elem ) || get_map().obscured_by_vehicle_rotation( last_point, elem ) ) {
                break;
            }
            g->m.add_field( elem, fd_dazzling, 2 );
            last_point = elem;
        }
    }

    z.clear_items();

    explosion_handler::explosion( z.pos(), &z, 8 );
    explosion_handler::get_explosion_queue().execute();
}

void mdeath::broken( monster &z )
{
    // Bail out if flagged (simulates eyebot flying away)
    if( z.no_corpse_quiet ) {
        return;
    }
    std::string item_id = z.type->id.str();
    if( item_id.starts_with( "mon_" ) ) {
        item_id.erase( 0, 4 );
    }
    // make "broken_manhack", or "broken_eyebot", ...
    item_id.insert( 0, "broken_" );
    detached_ptr<item> broken_mon = item::spawn( item_id, calendar::turn );
    const int max_hp = std::max( z.get_hp_max(), 1 );
    const float overflow_damage = std::max( -z.get_hp(), 0 );
    const float corpse_damage = 2.5 * overflow_damage / max_hp;
    broken_mon->set_damage( static_cast<int>( std::floor( corpse_damage * itype::damage_scale ) ) );
    item &broken_mon_ref = *broken_mon;
    g->m.add_item_or_charges( z.pos(), std::move( broken_mon ) );
    //TODO!: push up these temporaries
    if( z.type->has_flag( MF_DROPS_AMMO ) ) {
        for( const std::pair<const itype_id, int> &ammo_entry : z.type->starting_ammo ) {
            if( z.ammo[ammo_entry.first] > 0 ) {
                bool spawned = false;
                for( const std::pair<const std::string, mtype_special_attack> &attack : z.type->special_attacks ) {
                    if( attack.second->id == "gun" ) {
                        item &gun = *item::spawn_temporary( dynamic_cast<const gun_actor *>
                                                            ( attack.second.get() )->gun_type );
                        bool same_ammo = false;
                        for( const ammotype &at : gun.ammo_types() ) {
                            if( at == item::spawn_temporary( ammo_entry.first )->ammo_type() ) {
                                same_ammo = true;
                                break;
                            }
                        }
                        const bool uses_mags = !gun.magazine_compatible().empty();
                        if( same_ammo && uses_mags ) {
                            std::vector<detached_ptr<item>> mags;
                            int ammo_count = z.ammo[ammo_entry.first];
                            while( ammo_count > 0 ) {
                                detached_ptr<item> mag = item::spawn( gun.type->magazine_default.find( item::spawn_temporary(
                                        ammo_entry.first )->ammo_type() )->second );
                                mag->ammo_set( ammo_entry.first,
                                               std::min( ammo_count, mag->type->magazine->capacity ) );
                                ammo_count -= mag->type->magazine->capacity;
                                mags.push_back( std::move( mag ) );
                            }
                            g->m.spawn_items( z.pos(), std::move( mags ) );
                            spawned = true;
                            break;
                        }
                    }
                }
                if( !spawned ) {
                    g->m.spawn_item( z.pos(), ammo_entry.first, z.ammo[ammo_entry.first], 1,
                                     calendar::turn );
                }
            }
        }
    }

    // TODO: make mdeath::splatter work for robots
    if( ( broken_mon_ref.damage() >= broken_mon_ref.max_damage() ) && g->u.sees( z.pos() ) ) {
        add_msg( m_good, _( "The %s is destroyed!" ), z.name() );
    } else if( g->u.sees( z.pos() ) ) {
        add_msg( m_good, _( "The %s collapses!" ), z.name() );
    }
}

void mdeath::ratking( monster &z )
{
    g->u.remove_effect( effect_rat );
    if( g->u.sees( z ) ) {
        add_msg( m_warning, _( "Rats suddenly swarm into view." ) );
    }

    for( int rats = 0; rats < 7; rats++ ) {
        g->place_critter_around( mon_sewer_rat, z.pos(), 1 );
    }
}

void mdeath::darkman( monster &z )
{
    g->u.remove_effect( effect_darkness );
    if( g->u.sees( z ) ) {
        add_msg( m_good, _( "The %s melts away." ), z.name() );
    }
}

void mdeath::gas( monster &z )
{
    std::string explode = string_format( _( "a %s explode!" ), z.name() );
    sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "small" );
    g->m.emit_field( z.pos(), emit_id( "emit_toxic_blast" ) );
}

void mdeath::smokeburst( monster &z )
{
    std::string explode = string_format( _( "a %s explode!" ), z.name() );
    sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "small" );
    g->m.emit_field( z.pos(), emit_id( "emit_smoke_blast" ) );
}

void mdeath::fungalburst( monster &z )
{
    // If the fungus died from anti-fungal poison, don't pouf
    if( g->m.get_field_intensity( z.pos(), fd_fungicidal_gas ) ) {
        if( g->u.sees( z ) ) {
            add_msg( m_good, _( "The %s inflates and melts away." ), z.name() );
        }
        return;
    }

    std::string explode = string_format( _( "a %s explodes!" ), z.name() );
    sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "small" );
    g->m.emit_field( z.pos(), emit_id( "emit_fungal_blast" ) );
}

void mdeath::jabberwock( monster &z )
{
    player *ch = dynamic_cast<player *>( z.get_killer() );

    bool vorpal = ch && ch->is_player() &&
                  ( ch->primary_weapon().damage_melee( DT_CUT ) >= ( ch->primary_weapon().damage_melee(
                              DT_BASH ) * 1.5 ) ) &&
                  ch->primary_weapon().volume() > 500_ml;

    if( vorpal && !ch->primary_weapon().has_technique( matec_id( "VORPAL" ) ) ) {
        if( ch->sees( z ) ) {
            ch->add_msg_if_player( m_info,
                                   //~ %s is the possessive form of the monster's name
                                   _( "As the flames in %s eyes die out, your weapon seems to shine slightly brighter." ),
                                   z.disp_name( true ) );
        }
        ch->primary_weapon().add_technique( matec_id( "VORPAL" ) );
    }

    mdeath::normal( z );
}

void mdeath::gameover( monster &z )
{
    add_msg( m_bad, _( "The %s was destroyed!  GAME OVER!" ), z.name() );
    g->u.set_part_hp_cur( bodypart_id( "torso" ), 0 );
}

void mdeath::kill_breathers( monster &/*z*/ )
{
    for( monster &critter : g->all_monsters() ) {
        const mtype_id &monID = critter.type->id;
        if( monID == mon_breather_hub || monID == mon_breather ) {
            critter.die( nullptr );
        }
    }
}

void mdeath::detonate( monster &z )
{
    weighted_int_list<itype_id> amm_list;
    for( const auto &amm : z.ammo ) {
        amm_list.add( amm.first, amm.second );
    }

    std::vector<itype_id> pre_dets;
    for( int i = 0; i < 3; i++ ) {
        if( amm_list.get_weight() <= 0 ) {
            break;
        }
        // Grab one item
        itype_id tmp = *amm_list.pick();
        // and reduce its weight by 1
        amm_list.add_or_replace( tmp, amm_list.get_specific_weight( tmp ) - 1 );
        // and stash it for use
        pre_dets.push_back( tmp );
    }

    // Update any hardcoded explosion equivalencies
    std::vector<std::pair<std::string, long>> dets;
    for( const itype_id &bomb_id : pre_dets ) {
        if( bomb_id.str() == "bot_grenade_hack" ) {
            dets.emplace_back( "grenade_act", 5 );
        } else if( bomb_id.str() == "bot_flashbang_hack" ) {
            dets.emplace_back( "flashbang_act", 5 );
        } else if( bomb_id.str() == "bot_gasbomb_hack" ) {
            dets.emplace_back( "gasbomb_act", 20 );
        } else if( bomb_id.str() == "bot_c4_hack" ) {
            dets.emplace_back( "c4armed", 10 );
        } else if( bomb_id.str() == "bot_mininuke_hack" ) {
            dets.emplace_back( "mininuke_act", 20 );
        } else {
            // Get the transformation item
            const iuse_transform *actor = dynamic_cast<const iuse_transform *>(
                                              bomb_id->get_use( "transform" )->get_actor_ptr() );
            if( actor == nullptr ) {
                // Invalid bomb item, move to the next ammo item
                add_msg( m_debug, "Invalid bomb type in detonate mondeath for %s.", z.name() );
                continue;
            }
            dets.emplace_back( actor->target.str(), actor->ammo_qty );
        }
    }

    if( g->u.sees( z ) ) {
        if( dets.empty() ) {
            add_msg( m_info,
                     //~ %s is the possessive form of the monster's name
                     _( "The %s's hands fly to its pockets, but there's nothing left in them." ),
                     z.name() );
        } else {
            //~ %s is the possessive form of the monster's name
            add_msg( m_bad, _( "The %s's hands fly to its remaining pockets, opening them!" ),
                     z.name() );
        }
    }
    // HACK, used to stop them from having ammo on respawn
    z.add_effect( effect_no_ammo, 1_turns );

    // First die normally
    mdeath::normal( z );
    // Then detonate our suicide bombs
    for( const auto &bombs : dets ) {
        detached_ptr<item> bomb_item = item::spawn( bombs.first, calendar::start_of_cataclysm );
        bomb_item->charges = bombs.second;
        bomb_item->activate();
        g->m.add_item_or_charges( z.pos(), std::move( bomb_item ) );
    }
}

void mdeath::broken_ammo( monster &z )
{
    if( g->u.sees( z.pos() ) ) {
        //~ %s is the possessive form of the monster's name
        add_msg( m_info, _( "The %s's interior compartment sizzles with destructive energy." ),
                 z.name() );
    }
    mdeath::broken( z );
}

static std::vector<detached_ptr<item>> butcher_cbm_item( const itype_id &what,
                                    const time_point &birthday, const std::vector<flag_id> &flags,
                                    const std::vector<fault_id> &faults )
{
    detached_ptr<item> something = item::spawn( what, birthday );
    for( const flag_id &flg : flags ) {
        something->set_flag( flg );
    }
    for( const fault_id &flt : faults ) {
        something->faults.emplace( flt );
    }
    std::vector<detached_ptr<item>> ret;
    ret.push_back( std::move( something ) );
    return ret;
}

static std::vector<detached_ptr<item>> butcher_cbm_group( const item_group_id &group,
                                    const time_point &birthday, const std::vector<flag_id> &flags,
                                    const std::vector<fault_id> &faults )
{
    std::vector<detached_ptr<item>> spawned = item_group::items_from( group, birthday );
    for( detached_ptr<item> &it : spawned ) {
        for( const flag_id &flg : flags ) {
            it->set_flag( flg );
        }
        for( const fault_id &flt : faults ) {
            it->faults.emplace( flt );
        }
    }
    return spawned;
}

void make_mon_corpse( monster &z, int damageLvl )
{
    detached_ptr<item> corpse = item::make_corpse( z.type->id, calendar::turn, z.unique_name,
                                z.get_upgrade_time() );
    corpse->set_damage( damageLvl );
    if( z.has_effect( effect_pacified ) && z.type->in_species( ZOMBIE ) ) {
        // Pacified corpses have a chance of becoming unpacified when regenerating.
        corpse->set_var( "zlave", one_in( 2 ) ? "zlave" : "mutilated" );
    }
    if( z.has_effect( effect_no_ammo ) ) {
        corpse->set_var( "no_ammo", "no_ammo" );
    }
    if( !z.no_extra_death_drops ) {
        // Pre-gen bionic on death rather than on butcher
        for( const harvest_entry &entry : *z.type->harvest ) {
            if( entry.type == "bionic" || entry.type == "bionic_group" ) {
                std::vector<detached_ptr<item>> contained_bionics =
                                                 entry.type == "bionic"
                                                 ? butcher_cbm_item( itype_id( entry.drop ), calendar::turn, entry.flags, entry.faults )
                                                 : butcher_cbm_group( item_group_id( entry.drop ), calendar::turn, entry.flags, entry.faults );
                for( detached_ptr<item> &it : contained_bionics ) {
                    // Disgusting hack: use components instead of contents to hide stuff
                    corpse->add_component( std::move( it ) );
                }
            }
        }
    }
    for( detached_ptr<item> &it : z.remove_corpse_components() ) {
        corpse->add_component( std::move( it ) );
    }
    get_map().add_item_or_charges( z.pos(), std::move( corpse ) );
}

void mdeath::preg_roach( monster &z )
{
    int num_roach = rng( 1, 3 );
    while( num_roach > 0 && g->place_critter_around( mon_giant_cockroach_nymph, z.pos(), 1 ) ) {
        num_roach--;
        if( g->u.sees( z ) ) {
            add_msg( m_warning, _( "A cockroach nymph crawls out of the pregnant giant cockroach corpse." ) );
        }
    }
}

void mdeath::fireball( monster &z )
{
    if( one_in( 10 ) ) {
        g->m.propagate_field( z.pos(), fd_fire, 15, 3 );
        std::string explode = string_format( _( "an explosion of tank of the %s's flamethrower!" ),
                                             z.name() );
        sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "default" );
        add_msg( m_good, _( "I love the smell of burning zed in the morning." ) );
    } else {
        normal( z );
    }
}

void mdeath::conflagration( monster &z )
{
    for( const auto &dest : g->m.points_in_radius( z.pos(), 1 ) ) {
        g->m.propagate_field( dest, fd_fire, 18, 3 );
    }
    const std::string explode = string_format( _( "a %s explode!" ), z.name() );
    sounds::sound( z.pos(), 24, sounds::sound_t::combat, explode, false, "explosion", "small" );

}
