#include "gtfs_parser.h"
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <fstream>
#include <iostream>
#include <deque>
#include <set>
#include "utils/encoding_converter.h"
#include "utils/csv.h"
#include <utils/logger.h>

namespace nm = navimake::types;
typedef boost::tokenizer< boost::escaped_list_separator<char> > Tokenizer;

namespace navimake{ namespace connectors {


int time_to_int(const std::string & time) {
    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep(":");
    tokenizer tokens(time, sep);
    std::vector<std::string> elts(tokens.begin(), tokens.end());
    int result = 0;
    if(elts.size() != 3)
        return -1;
    try {
        result = boost::lexical_cast<int>(elts[0]) * 3600;
        result += boost::lexical_cast<int>(elts[1]) * 60;
        result += boost::lexical_cast<int>(elts[2]);
    }
    catch(boost::bad_lexical_cast){
        return -1;
    }
    return result;
}


GtfsParser::GtfsParser(const std::string & path) :
    path(path), production_date(boost::gregorian::date(), boost::gregorian::date()){
        init_logger();
        logger = log4cplus::Logger::getInstance("log");
    }


void GtfsParser::fill(Data & data, const std::string beginning_date){
    try {
        production_date = find_production_date(beginning_date);
    } catch (...) {
        if(beginning_date == "") {
            LOG4CPLUS_FATAL(logger, "Impossible de trouver la date de production");
            return;
        }
    }

    fill_modes(data);
    typedef boost::function<void(GtfsParser*, Data&, CsvReader&)> parse_function;
    typedef std::pair<std::string, parse_function> string_function;
    std::vector<string_function> filename_function_list;
    filename_function_list.push_back(std::make_pair("agency.txt", &GtfsParser::parse_agency));
    filename_function_list.push_back(std::make_pair("stops.txt", &GtfsParser::parse_stops));
    filename_function_list.push_back(std::make_pair("routes.txt", &GtfsParser::parse_lines));
    filename_function_list.push_back(std::make_pair("transfers.txt", &GtfsParser::parse_transfers));
    filename_function_list.push_back(std::make_pair("calendar.txt", &GtfsParser::parse_calendar));
    filename_function_list.push_back(std::make_pair("calendar_dates.txt", &GtfsParser::parse_calendar_dates));
    filename_function_list.push_back(std::make_pair("trips.txt", &GtfsParser::parse_trips));
    filename_function_list.push_back(std::make_pair("stop_times.txt", &GtfsParser::parse_stop_times));
    filename_function_list.push_back(std::make_pair("frequencies.txt", &GtfsParser::parse_frequencies));
    
    std::set<std::string> required_files = {"agency.txt", "stops.txt", 
        "routes.txt", "trips.txt", "stop_times.txt"};
    
    for(auto filename_function : filename_function_list) {
        LOG4CPLUS_TRACE(logger, "On parse : " + filename_function.first);
        CsvReader csv(path+ "/" + filename_function.first, ',' , true);
        if(!csv.is_open()) {
            if(required_files.find(filename_function.first) != required_files.end()) {
                LOG4CPLUS_FATAL(logger, "Impossible d'ouvrir " + filename_function.first);
                throw FileNotFoundException(filename_function.first);
            } else {
                LOG4CPLUS_WARN(logger, "Impossible d'ouvrir " + filename_function.first);
            }
        } else {
            filename_function.second(this, data, csv);
        }
    }

    normalize_extcodes(data);
}


void GtfsParser::fill_modes(Data & data) {
    navimake::types::CommercialMode* commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "0";
    commercial_mode->name = "Tram";
    commercial_mode->uri = "0x0";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "1";
    commercial_mode->name = "Metro";
    commercial_mode->uri = "0x1";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "2";
    commercial_mode->name = "Rail";
    commercial_mode->uri = "0x2";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "3";
    commercial_mode->name = "Bus";
    commercial_mode->uri = "0x3";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "4";
    commercial_mode->name = "Ferry";
    commercial_mode->uri = "0x4";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "5";
    commercial_mode->name = "Cable car";
    commercial_mode->uri = "0x5";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "6";
    commercial_mode->name = "Gondola";
    commercial_mode->uri = "0x6";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    commercial_mode = new navimake::types::CommercialMode();
    commercial_mode->id = "7";
    commercial_mode->name = "Funicular";
    commercial_mode->uri = "0x7";
    data.commercial_modes.push_back(commercial_mode);
    commercial_mode_map[commercial_mode->id] = commercial_mode;

    BOOST_FOREACH(navimake::types::CommercialMode *mt, data.commercial_modes) {
        navimake::types::PhysicalMode* mode = new navimake::types::PhysicalMode();
        mode->id = mt->id;
        mode->name = mt->name;
        mode->uri = mt->uri;
        data.physical_modes.push_back(mode);
        mode_map[mode->id] = mode;
    }
}


void GtfsParser::parse_agency(Data & data, CsvReader & csv){
    std::vector<std::string> headers = {"agency_name", "agency_url", "agency_timezone"};
    if(!csv.validate(headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors de la lecture " + csv.filename +
                " il manque les colonnes  : " + csv.missing_headers(headers));
        throw InvalidHeaders(csv.filename);
    }

    int id_c = csv.get_pos_col("agency_id"), name_c = csv.get_pos_col("agency_name");

    bool line_read = false;
    while(!csv.eof()) {
        auto row = csv.next();
        if(!row.empty()) {
            if(line_read && id_c == -1) {
                LOG4CPLUS_FATAL(logger, "Erreur lors de la lecture " + csv.filename +
                        "le fichier comporte plus d'une agence et n'a pas de colonne agency_id");
                throw InvalidHeaders(csv.filename);
            }
            nm::Network * network = new nm::Network();
            if(id_c != -1)
                network->uri = row[id_c];
            else
                network->uri = "default_agency";
            network->name = row[name_c];
            data.networks.push_back(network);
            agency_map[network->uri] = network;
            line_read = true;
        }
    }
}


void GtfsParser::parse_stops(Data & data, CsvReader & csv) {
    std::vector<std::string> mandatory_headers = {"stop_id" ,  "stop_name",
        "stop_lat", "stop_lon"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger,  "Erreur lors du parsing de " + csv.filename 
                  +". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }
    // On réserve un peu large, de l'ordre de l'Île-de-France
    data.stop_points.reserve(56000);
    data.stop_areas.reserve(13000);

    int id_c = csv.get_pos_col("stop_id"), code_c = csv.get_pos_col("stop_code"),
        lat_c = csv.get_pos_col("stop_lat"), lon_c = csv.get_pos_col("stop_lon"),
        type_c = csv.get_pos_col("location_type"), parent_c = csv.get_pos_col("parent_station"),
        name_c = csv.get_pos_col("stop_name"), wheelchair_c = csv.get_pos_col("wheelchair_boarding");

    if(code_c == -1){
        code_c = id_c;
    }

    int ignored = 0;
    std::vector<types::StopPoint*> wheelchair_heritance;
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            continue;
        nm::StopPoint * sp = new nm::StopPoint();
        try{
            sp->coord.set_lon(boost::lexical_cast<double>(row[lon_c]));
            sp->coord.set_lat(boost::lexical_cast<double>(row[lat_c]));
        }
        catch(boost::bad_lexical_cast ) {
            LOG4CPLUS_WARN(logger, "Impossible de parser les coordonnées pour " 
                    + row[id_c] + " " + row[code_c] + " " + row[name_c]);
        }

        sp->name = row[name_c];
        sp->uri = row[id_c];
        //On teste si c'est un doublon, daqns gtfs le meme fichier contient stoparea et stoppoint
        if(stop_map.find(sp->uri) != stop_map.end() ||
           stop_area_map.find(sp->uri) != stop_area_map.end()) {
            LOG4CPLUS_WARN(logger, "Le stop " + sp->uri +" a ete ignore");
            ignored++;
        } else {
            // Si c'est un stopArea
            if(type_c != -1 && row[type_c] == "1") {
                nm::StopArea * sa = new nm::StopArea();
                sa->coord = sp->coord;
                sa->name = sp->name;
                sa->uri = sp->uri;
                if(wheelchair_c != -1)
                    sa->wheelchair_boarding = row[wheelchair_c] == "1";
                stop_area_map[sa->uri] = sa;
                data.stop_areas.push_back(sa);
                delete sp;
            }
            // C'est un StopPoint
            else {
                if(wheelchair_c != -1) {
                    if(row[wheelchair_c] == "0") {
                        wheelchair_heritance.push_back(sp);
                    } else {
                        sp->wheelchair_boarding = row[wheelchair_c] == "1";
                    }
                }
                stop_map[sp->uri] = sp;
                data.stop_points.push_back(sp);
                if(parent_c!=-1 && row[parent_c] != "") {// On sauvegarde la référence à la zone d'arrêt
                    if(sa_spmap.find(row[parent_c]) == sa_spmap.end()) {
                        sa_spmap.insert(std::make_pair(row[parent_c], vector_sp()));
                    }
                    sa_spmap[row[parent_c]].push_back(sp);
                }
            }
        }
    }

    // On reboucle pour récupérer les stop areas de tous les stop points
    for(auto sa_sps : sa_spmap) {
        if(stop_area_map.find(sa_sps.first) != stop_area_map.end()) {
            auto it = stop_area_map.find(sa_sps.first);
            for(auto sp : sa_sps.second) {
                sp->stop_area = it->second;
            }
        } else {
            std::string error_message = 
                "Le stop area " + sa_sps.first  + " n'a pas été trouvé pour les stops points :  ";
            for(auto sp : sa_sps.second) {
                error_message += sp->uri;
                sp->stop_area = 0;
            }
            LOG4CPLUS_WARN(logger, error_message);
        }
    }

    //On va chercher l'accessibilité pour les stop points qui hérite de l'accessibilité de leur stop area
    for(auto sp : wheelchair_heritance) {
        if(sp->stop_area != 0) {
            sp->wheelchair_boarding = sp->stop_area->wheelchair_boarding;
        } else {
            LOG4CPLUS_WARN(logger, "Impossible de récuperer l'accessibilité du stop area pour le stop point " + sp->uri);
        }
    }
    LOG4CPLUS_TRACE(logger, "J'ai parsé " + boost::lexical_cast<std::string>(data.stop_points.size()) + " stop points");;
    LOG4CPLUS_TRACE(logger, "J'ai parsé " + boost::lexical_cast<std::string>(data.stop_areas.size())+ " stop areas");
    LOG4CPLUS_TRACE(logger, "J'ai ignoré " + boost::lexical_cast<std::string>(ignored)+ " points à cause de doublons" );
}


void GtfsParser::parse_transfers(Data & data, CsvReader & csv) {
    std::string filename = path + "/transfers.txt";

    std::vector<std::string> mandatory_headers = {"from_stop_id", "to_stop_id"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }

    int from_c = csv.get_pos_col("from_stop_id"),
        to_c = csv.get_pos_col("to_stop_id"),
        time_c = csv.get_pos_col("min_transfer_time");

    int nblines = 0;
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            continue;
        typedef std::unordered_map<std::string, navimake::types::StopPoint*>::iterator sp_iterator;
        vector_sp departures, arrivals;
        std::unordered_map<std::string, navimake::types::StopPoint*>::iterator it;
        it = this->stop_map.find(row[from_c]);
        if(it == this->stop_map.end()){
            std::unordered_map<std::string, vector_sp>::iterator it_sa = this->sa_spmap.find(row[from_c]);
            if(it_sa == this->sa_spmap.end()) {
                LOG4CPLUS_WARN(logger, "Impossible de trouver le stop point (from) " + row[from_c]);
                continue;
            } else {
                departures = it_sa->second;
            }
        } else {
            departures.push_back(it->second);
        }

        it = this->stop_map.find(row[to_c]);
        if(it == this->stop_map.end()){
            std::unordered_map<std::string, vector_sp>::iterator it_sa = this->sa_spmap.find(row[to_c]);
            if(it_sa == this->sa_spmap.end()) {
                 LOG4CPLUS_WARN(logger, "Impossible de trouver le stop point (to) " + row[to_c]);
                continue;
            } else {
                arrivals = it_sa->second;
            }
        } else {
            arrivals.push_back(it->second);
        }

        for(auto from_sp : departures) {
            for(auto to_sp : arrivals) {
                nm::Connection * connection = new nm::Connection();
                connection->departure_stop_point = from_sp;
                connection->destination_stop_point  = to_sp;
                connection->connection_kind = nm::Connection::LinkConnection;
                if(time_c != -1) {
                    try{
                        connection->duration = boost::lexical_cast<int>(row[time_c]);
                    } catch (...) {
                        connection->duration = 120;
                    }
                } else {
                   connection->duration = 120;
                }

                data.connections.push_back(connection);
            }
        }
        nblines++;
    }

    LOG4CPLUS_TRACE(logger, boost::lexical_cast<std::string>(nblines) + 
            " correspondances prises en compte sur " + boost::lexical_cast<std::string>(data.connections.size()));
}


void GtfsParser::parse_calendar(Data & data, CsvReader & csv) {
    std::vector<std::string> mandatory_headers = {"service_id" , "monday", "tuesday",
        "wednesday", "thursday", "friday", "saturday", "sunday", "start_date", "end_date"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger,  "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }
    data.validity_patterns.reserve(10000);

    int id_c = csv.get_pos_col("service_id"), monday_c = csv.get_pos_col("monday"),
        tuesday_c = csv.get_pos_col("tuesday"), wednesday_c = csv.get_pos_col("wednesday"),
        thursday_c = csv.get_pos_col("thursday"), friday_c = csv.get_pos_col("friday"),
        saturday_c = csv.get_pos_col("saturday"), sunday_c = csv.get_pos_col("sunday"),
        start_date_c = csv.get_pos_col("start_date"), end_date_c = csv.get_pos_col("end_date");
    std::bitset<7> week;
    unsigned int nblignes = 0;
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            break;
        nblignes ++;
        nm::ValidityPattern * vp;
        std::unordered_map<std::string, nm::ValidityPattern*>::iterator it= vp_map.find(row[id_c]);
        if(it == vp_map.end()){
            //On initialise la semaine
            week[1] = (row[monday_c] == "1");
            week[2] = (row[tuesday_c] == "1");
            week[3] = (row[wednesday_c] == "1");
            week[4] = (row[thursday_c] == "1");
            week[5] = (row[friday_c] == "1");
            week[6] = (row[saturday_c] == "1");
            week[0] = (row[sunday_c] == "1");

            vp = new nm::ValidityPattern(production_date.begin());

            for(unsigned int i = 0; i<366;++i)
                vp->remove(i);

            //Initialisation des jours de la date de départ jusqu'à la date de fin du service
            boost::gregorian::date b_date = boost::gregorian::from_undelimited_string(row[start_date_c]);
            boost::gregorian::date_period period = boost::gregorian::date_period((b_date > production_date.begin() ? b_date : production_date.begin()), boost::gregorian::from_undelimited_string(row[end_date_c]));
            for(boost::gregorian::day_iterator it(period.begin()); it<period.end(); ++it) {
                if(week.test((*it).day_of_week())) {
                    vp->add((*it));
                }
                else
                    vp->remove((*it));
            }

            vp->uri = row[id_c];
            vp_map[row[id_c]] = vp;
            data.validity_patterns.push_back(vp);

        }
        else {
            vp = it->second;
        }
    }
    BOOST_ASSERT(data.validity_patterns.size() == vp_map.size());
    LOG4CPLUS_TRACE(logger, "Nombre de validity patterns : " +
            boost::lexical_cast<std::string>(data.validity_patterns.size())+"nb lignes : " +
            boost::lexical_cast<std::string>(nblignes));

}


void GtfsParser::parse_calendar_dates(Data & data, CsvReader & csv){
    std::vector<std::string> mandatory_headers = {"service_id", "date", "exception_type"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }

    int id_c = csv.get_pos_col("service_id"), date_c = csv.get_pos_col("date"),
        e_type_c = csv.get_pos_col("exception_type");
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            break;
        nm::ValidityPattern * vp;
        std::unordered_map<std::string, nm::ValidityPattern*>::iterator it= vp_map.find(row[id_c]);
        if(it == vp_map.end()){
            vp = new nm::ValidityPattern(production_date.begin());
            vp_map[row[id_c]] = vp;
            data.validity_patterns.push_back(vp);
        }
        else {
            vp = it->second;
        }

        auto date = boost::gregorian::from_undelimited_string(row[date_c]);
        if(row[e_type_c] == "1")
            vp->add(date);
        else if(row[e_type_c] == "2")
            vp->remove(date);
        else
            LOG4CPLUS_WARN(logger, "Exception pour le service " + row[id_c] + " inconnue : " + row[e_type_c]);
    }
    BOOST_ASSERT(data.validity_patterns.size() == vp_map.size());
    LOG4CPLUS_TRACE(logger, "Nombre de validity patterns : " + boost::lexical_cast<std::string>(data.validity_patterns.size()));
}


void GtfsParser::parse_lines(Data & data, CsvReader &csv){
    std::vector<std::string> mandatory_headers = {"route_id", "route_short_name",
        "route_long_name", "route_type"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }
    data.lines.reserve(10000);

    int id_c = csv.get_pos_col("route_id"), short_name_c = csv.get_pos_col("route_short_name"),
        long_name_c = csv.get_pos_col("route_long_name"), type_c = csv.get_pos_col("route_type"),
        color_c = csv.get_pos_col("route_color"), agency_c = csv.get_pos_col("agency_id");
    int ignored = 0;
    
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            break;
        if(line_map.find(row[id_c]) == line_map.end()) {
            nm::Line * line = new nm::Line();
            line->uri = row[id_c];
            line->name = row[long_name_c];
            line->code = row[short_name_c];
            if(color_c != -1)
                line->color = row[color_c];
            line->additional_data = row[long_name_c];

            std::unordered_map<std::string, nm::CommercialMode*>::iterator it= commercial_mode_map.find(row[type_c]);
            if(it != commercial_mode_map.end())
                line->commercial_mode = it->second;
            if(agency_c != -1) {
                auto agency_it = agency_map.find(row[agency_c]);
                if(agency_it != agency_map.end())
                    line->network = agency_it->second;
            }
            else {
                auto agency_it = agency_map.find("default_agency");
                if(agency_it != agency_map.end())
                    line->network = agency_it->second;
            }

            line_map[row[id_c]] = line;
            data.lines.push_back(line);
        }
        else {
            ignored++;
            LOG4CPLUS_WARN(logger, "Doublon de la ligne " + row[id_c]);
        }
    }
    BOOST_ASSERT(data.lines.size() == line_map.size());
    LOG4CPLUS_TRACE(logger, "Nombre de lignes : " + boost::lexical_cast<std::string>(data.lines.size()));
    LOG4CPLUS_TRACE(logger, "J'ai ignoré " + boost::lexical_cast<std::string>(ignored) + " lignes pour cause de doublons");
}


void GtfsParser::parse_trips(Data & data, CsvReader &csv) {
    std::vector<std::string> mandatory_headers = {"route_id", "service_id",
        "trip_id"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }
    data.journey_patterns.reserve(350000);
    data.vehicle_journeys.reserve(350000);

    int id_c = csv.get_pos_col("route_id"), service_c = csv.get_pos_col("service_id"),
        trip_c = csv.get_pos_col("trip_id"), headsign_c = csv.get_pos_col("trip_headsign"),
        block_id_c = csv.get_pos_col("block_id"), wheelchair_c = csv.get_pos_col("wheelchair_accessible");

    int ignored = 0;
    int ignored_vj = 0;
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            break;

        std::unordered_map<std::string, nm::Line*>::iterator it = line_map.find(row[id_c]);
        if(it == line_map.end()){
            LOG4CPLUS_WARN(logger, "Impossible de trouver la Route (au sens GTFS) " + row[id_c]
                      + " référencée par trip " + row[trip_c]);
            ignored++;
        }
        else {
            nm::Line * line = it->second;
            std::unordered_map<std::string, nm::PhysicalMode*>::iterator itm = mode_map.find(line->commercial_mode->id);
            if(itm == mode_map.end()) {
                LOG4CPLUS_WARN(logger, "Impossible de trouver le mode (au sens GTFS) " + line->commercial_mode->id
                          + " référencée par trip " + row[trip_c]);
                ignored++;
            } else {
                nm::ValidityPattern * vp_xx;
                std::unordered_map<std::string, nm::ValidityPattern*>::iterator vp_it = vp_map.find(row[service_c]);
                if(vp_it == vp_map.end()) {
                    ignored++;
                    continue;
                }
                else {
                    vp_xx = vp_it->second;
                }

                std::unordered_map<std::string, nm::VehicleJourney*>::iterator vj_it = vj_map.find(row[trip_c]);
                if(vj_it == vj_map.end()) {
                    nm::VehicleJourney * vj = new nm::VehicleJourney();
                    vj->uri = row[trip_c];
                    if(headsign_c != -1)
                        vj->name = row[headsign_c];
                    else
                        vj->name = vj->uri;
                    vj->validity_pattern = vp_xx;
                    vj->journey_pattern = 0;
                    vj->tmp_line = line;
                    vj->physical_mode = itm->second;
                    if(block_id_c != -1)
                        vj->block_id = row[block_id_c];
                    else
                        vj->block_id = "";
                    if(wheelchair_c != -1)
                        vj->wheelchair_boarding = row[wheelchair_c] == "1";
                    vj_map[vj->uri] = vj;
                    data.vehicle_journeys.push_back(vj);
                }
                else {
                    ignored_vj++;
                }
            }
        }
    }

    BOOST_ASSERT(data.vehicle_journeys.size() == vj_map.size());
    LOG4CPLUS_TRACE(logger, "Nombre de vehicle journeys : " + boost::lexical_cast<std::string>(data.vehicle_journeys.size()));
    LOG4CPLUS_TRACE(logger, "Nombre d'erreur de référence de service : " + boost::lexical_cast<std::string>(ignored));
    LOG4CPLUS_TRACE(logger, "J'ai ignoré "+ boost::lexical_cast<std::string>(ignored_vj) + " vehicule journey à cause de doublons");
}

void GtfsParser::parse_frequencies(Data &, CsvReader &csv) {
    std::vector<std::string> mandatory_headers = {"trip_id", "start_time",
        "end_time", "headway_secs"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }
    int trip_id_c = csv.get_pos_col("trip_id"), start_time_c = csv.get_pos_col("start_time"),
        end_time_c = csv.get_pos_col("end_time"), headway_secs_c = csv.get_pos_col("headway_secs");

    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            break;
        std::unordered_map<std::string, nm::VehicleJourney*>::iterator vj_it = vj_map.find(row[trip_id_c]);
        if(vj_it != vj_map.end()) {
            int begin = vj_it->second->stop_time_list.front()->arrival_time;
            for(auto st_it = vj_it->second->stop_time_list.begin(); st_it != vj_it->second->stop_time_list.end(); ++st_it) {
                (*st_it)->start_time = time_to_int(row[start_time_c]) + (*st_it)->arrival_time - begin;
                (*st_it)->end_time = time_to_int(row[end_time_c]) + (*st_it)->arrival_time - begin;
                (*st_it)->headway_secs = boost::lexical_cast<int>(row[headway_secs_c]);
                (*st_it)->is_frequency = true;
            }
        }
    }
}




void normalize_extcodes(Data & data){
    for(nm::StopArea * sa : data.stop_areas){
        boost::algorithm::replace_first(sa->uri, "StopArea:", "");
    }
    for(nm::StopPoint * sp : data.stop_points){
        boost::algorithm::replace_first(sp->uri, "StopPoint:", "");
    }
}




void GtfsParser::parse_stop_times(Data & data, CsvReader &csv) {
    std::vector<std::string> mandatory_headers = {"trip_id" , "arrival_time",
        "departure_time", "stop_id", "stop_sequence"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + csv.filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        throw InvalidHeaders(csv.filename);
    }
    if(csv.eof())
        return;

    int id_c = csv.get_pos_col("trip_id"), arrival_c = csv.get_pos_col("arrival_time"),
        departure_c = csv.get_pos_col("departure_time"), stop_c = csv.get_pos_col("stop_id"),
        stop_seq_c = csv.get_pos_col("stop_sequence"), pickup_c = csv.get_pos_col("pickup_type"),
        drop_off_c = csv.get_pos_col("drop_off_type"), itl_c = csv.get_pos_col("stop_times_itl");


    size_t count = 0;
    while(!csv.eof()) {
        auto row = csv.next();
        if(row.empty())
            break;
        auto vj_it = vj_map.find(row[id_c]);
        if(vj_it == vj_map.end()) {
            LOG4CPLUS_WARN(logger, "Impossible de trouver le vehicle_journey " + row[id_c]);
            continue;
        }
        auto stop_it = stop_map.find(row[stop_c]);
        if(stop_it == stop_map.end()){
            LOG4CPLUS_WARN(logger, "Impossible de trouver le StopPoint " + row[stop_c] + "!");
        }
        else {
            nm::StopTime * stop_time = new nm::StopTime();
            stop_time->arrival_time = time_to_int(row[arrival_c]);
            stop_time->departure_time = time_to_int(row[departure_c]);
            stop_time->tmp_stop_point = stop_it->second;
            //stop_time->journey_pattern_point = journey_pattern_point;
            stop_time->order = boost::lexical_cast<int>(row[stop_seq_c]);
            stop_time->vehicle_journey = vj_it->second;
            if(pickup_c != -1 && drop_off_c != -1)
                stop_time->ODT = (row[pickup_c] == "2" && row[drop_off_c] == "2");
            else
                stop_time->ODT = false;
            if(pickup_c != -1)
                stop_time->pick_up_allowed = row[pickup_c] != "1";
            else
                stop_time->pick_up_allowed = true;
            if(drop_off_c != -1)
                stop_time->drop_off_allowed = row[drop_off_c] != "1";
            else
                stop_time->drop_off_allowed = true;
            if(itl_c != -1)
                stop_time->local_traffic_zone = boost::lexical_cast<int>(row[itl_c]);
            else
                stop_time->local_traffic_zone = std::numeric_limits<uint32_t>::max();
            stop_time->vehicle_journey->stop_time_list.push_back(stop_time);
            stop_time->wheelchair_boarding = stop_time->vehicle_journey->wheelchair_boarding;
            data.stops.push_back(stop_time);
            count++;
        }
    }
    LOG4CPLUS_TRACE(logger, "Nombre d'horaires : " + boost::lexical_cast<std::string>(data.stops.size()));
}

boost::gregorian::date_period GtfsParser::basic_production_date(const std::string beginning_date) {
    if(beginning_date == "") {
        throw UnableToFindProductionDateException();
    } else {
        boost::gregorian::date b_date(boost::gregorian::from_undelimited_string(beginning_date)),
                               e_date(b_date + boost::gregorian::date_duration(365));

        return boost::gregorian::date_period(b_date, e_date);
    }
}

boost::gregorian::date_period GtfsParser::find_production_date(const std::string beginning_date) {
    std::string filename = path + "/stop_times.txt";
    CsvReader csv(filename, ',' , true);
    if(!csv.is_open()) {
        LOG4CPLUS_WARN(logger, "Aucun fichier " + filename);
        return basic_production_date(beginning_date);
    }

    std::vector<std::string> mandatory_headers = {"trip_id" , "arrival_time",
        "departure_time", "stop_id", "stop_sequence"};
    if(!csv.validate(mandatory_headers)) {
        LOG4CPLUS_FATAL(logger, "Erreur lors du parsing de " + filename 
                  + ". Il manque les colonnes : " 
                  + csv.missing_headers(mandatory_headers));
        return basic_production_date(beginning_date);
    }
  

    std::map<std::string, bool> trips;
    int trip_c = csv.get_pos_col("trip_id");
    while(!csv.eof()) {
        auto row = csv.next();
        if(!row.empty())
            trips.insert(std::make_pair(row[trip_c], true));
    }

    filename = path + "/trips.txt";
    CsvReader csv2(filename, ',' , true);
    if(!csv2.is_open()) {
        LOG4CPLUS_WARN(logger, "Aucun fichier " + filename);
        return basic_production_date(beginning_date);
    }

    mandatory_headers = {"trip_id" , "service_id"};
    if(!csv2.validate(mandatory_headers)) {
        LOG4CPLUS_WARN(logger, "Erreur lors du parsing de " + filename 
                  + ". Il manque les colonnes : " 
                  + csv2.missing_headers(mandatory_headers));
        return basic_production_date(beginning_date);
    }
    int service_c = csv2.get_pos_col("service_id");
    trip_c = csv2.get_pos_col("trip_id");
    std::map<std::string, bool> services;
    while(!csv2.eof()) {
        auto row = csv2.next();
        if(!row.empty() && trips.find(row[trip_c]) != trips.end())
            services.insert(std::make_pair(row[service_c], true));
    }
    boost::gregorian::date start_date(boost::gregorian::max_date_time), end_date(boost::gregorian::min_date_time);
    
    filename = path + "/calendar.txt";
    CsvReader csv3(filename, ',' , true);
    bool calendar_txt_exists = false;
    if(!csv3.is_open()) {
        LOG4CPLUS_WARN(logger, "Aucun fichier " + filename);
    } else {
        calendar_txt_exists = true;
        mandatory_headers = {"start_date" , "end_date", "service_id"};
        if(!csv3.validate(mandatory_headers)) {
            LOG4CPLUS_WARN(logger, "Erreur lors du parsing de " + filename 
                    + ". Il manque les colonnes : " 
                    + csv3.missing_headers(mandatory_headers));
            return basic_production_date(beginning_date);
        }

        int start_date_c = csv3.get_pos_col("start_date"), end_date_c = csv3.get_pos_col("end_date");
        service_c = csv3.get_pos_col("service_id");


        while(!csv3.eof()) {
            auto row = csv3.next();
            if(!row.empty()) {
                if(services.find(row[service_c]) != services.end()) {
                    boost::gregorian::date current_start_date = boost::gregorian::from_undelimited_string(row[start_date_c]);
                    boost::gregorian::date current_end_date = boost::gregorian::from_undelimited_string(row[end_date_c]);

                    if(current_start_date < start_date){
                        start_date = current_start_date;
                    }
                    if(current_end_date > end_date){
                        end_date = current_end_date;
                    }
                }
            }
        }
    }

    filename = path + "/calendar_dates.txt";
    CsvReader csv4(filename, ',' , true);
    if(!csv4.is_open()) {
        if(calendar_txt_exists)
            LOG4CPLUS_WARN(logger, "Aucun fichier " + filename);
        else
            LOG4CPLUS_FATAL(logger, "Aucun fichiers " + filename + " ni calendar.txt");
    } else {
        mandatory_headers = {"service_id" , "date", "exception_type"};
        if(!csv4.validate(mandatory_headers)) {
            LOG4CPLUS_WARN(logger, "Erreur lors du parsing de " + filename 
                    + ". Il manque les colonnes : " 
                    + csv4.missing_headers(mandatory_headers));
            return basic_production_date(beginning_date);
        }
        int date_c = csv4.get_pos_col("date");
            service_c = csv4.get_pos_col("service_id");
        while(!csv4.eof()) {
            auto row = csv4.next();
            if(!row.empty() && services.find(row[service_c]) != services.end()) {
                boost::gregorian::date current_date = boost::gregorian::from_undelimited_string(row[date_c]);
                if(current_date < start_date){
                    start_date = current_date;
                }
                if(current_date > end_date){
                    end_date = current_date;
                }
            }
        }

    }
 

    boost::gregorian::date b_date(boost::gregorian::min_date_time);
    if(beginning_date != "")
        b_date = boost::gregorian::from_undelimited_string(beginning_date);
    LOG4CPLUS_TRACE(logger, "date de production: " +
        boost::gregorian::to_simple_string((start_date>b_date ? start_date : b_date)) 
        + " - " + boost::gregorian::to_simple_string(end_date));
    return boost::gregorian::date_period((start_date>b_date ? start_date : b_date), end_date);
}

}}
