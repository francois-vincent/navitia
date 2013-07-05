#pragma once
#include "types.h"

#include "type/type.h"
#include "type/data.h"
#include "type/datetime.h"

namespace nt = navitia::type;
/** Ce namespace contient toutes les structures de données \b temporaires, à remplir par le connecteur */
namespace navimake{

template<typename T>
void normalize_uri(std::vector<T*>& vec){
    std::string prefix = navitia::type::static_data::get()->captionByType(T::type);
    for(auto* element : vec){
        element->uri = prefix + ":" + element->uri;
    }
}

bool same_journey_pattern(types::VehicleJourney * vj1, types::VehicleJourney * vj2);

/// Ajoute une connection entre deux journey_pattern_point
void  add_journey_pattern_point_connection(types::JourneyPatternPoint *rp1, types::JourneyPatternPoint *rp2, int length,
                           std::multimap<std::string, types::JourneyPatternPointConnection> &journey_pattern_point_connections);

/** Structure de donnée temporaire destinée à être remplie par un connecteur
      *
      * Les vecteurs contiennent des pointeurs vers un objet TC.
      * Les relations entre objets TC sont gèrés par des pointeurs
      *
      */
class Data{
public:
#define NAVIMAKE_COLLECTIONS(type_name, collection_name) std::vector<types::type_name*> collection_name;
    ITERATE_NAVITIA_PT_TYPES(NAVIMAKE_COLLECTIONS)
    std::vector<types::StopTime*> stops;
    std::vector<types::JourneyPatternPointConnection*> journey_pattern_point_connections;
    std::vector<types::StopPointConnection*> stop_point_connections;


    /**
         * trie les différentes donnée et affecte l'idx
         *
         */
    void sort();

    // Sort qui fait erreur valgrind
    struct sort_vehicle_journey_list {
        const navitia::type::PT_Data & data;
        sort_vehicle_journey_list(const navitia::type::PT_Data & data) : data(data){}
        bool operator ()(const nt::VehicleJourney* vj1, const nt::VehicleJourney* vj2) const {
            if(!vj1->stop_time_list.empty() && !vj2->stop_time_list.empty()) {
                unsigned int dt1 = vj1->stop_time_list.front()->departure_time;
                unsigned int dt2 = vj2->stop_time_list.front()->departure_time;
                unsigned int at1 = vj1->stop_time_list.back()->arrival_time;
                unsigned int at2 = vj2->stop_time_list.back()->arrival_time;
                if(dt1 != dt2)
                    return dt1 < dt2;
                else
                    return at1 < at2;
            } else
                return false;
        }
    };

    /// Construit les journey_patterns en retrouvant les paterns à partir des VJ
    void build_journey_patterns();

    /// Construit les journey_patternpoint
    void build_journey_pattern_points();

    /// Construit les connections pour les correspondances garanties
    void build_journey_pattern_point_connections();

    void normalize_uri();

    /**
     * Ajoute des objets
     */
    void complete();


    /**
     * supprime les objets inutiles
     */
    void clean();

    /**
          * Transforme les les pointeurs en données
          */
    void transform(navitia::type::PT_Data& data);

    /**
          * Gère les relations
          */
    void build_relations(navitia::type::PT_Data & data);

    /// Construit le contour de la région à partir des stops points
    std::string compute_bounding_box(navitia::type::PT_Data &data);
    ~Data(){
#define DELETE_ALL_ELEMENTS(type_name, collection_name) for(auto element : collection_name) delete element;
        ITERATE_NAVITIA_PT_TYPES(DELETE_ALL_ELEMENTS)
        for(navimake::types::StopTime* stop : stops){
            delete stop;
        }
    }

};
}