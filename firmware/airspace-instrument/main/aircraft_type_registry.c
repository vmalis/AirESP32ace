// AirESP32ace
// Created by VM, August 2026

#include "aircraft_type_registry.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

typedef struct {
    const char *code;
    const char *slug;
    const char *label;
    airspace_aircraft_shape_t shape;
} exact_type_entry_t;

typedef struct {
    const char *needle;
    const char *code;
    const char *slug;
    const char *label;
    airspace_aircraft_shape_t shape;
} model_alias_entry_t;

static const exact_type_entry_t exact_types[] = {    {"BCS1","airbus-a220","Airbus A220",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"BCS3","airbus-a220","Airbus A220",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"A30B","airbus-a300","Airbus A300",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A306","airbus-a300","Airbus A300",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A310","airbus-a310","Airbus A310",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A318","airbus-a319","Airbus A319",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A319","airbus-a319","Airbus A319",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A19N","airbus-a319","Airbus A319",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A320","airbus-a320","Airbus A320",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A20N","airbus-a320","Airbus A320",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A321","airbus-a321","Airbus A321",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A21N","airbus-a321","Airbus A321",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"A332","airbus-a330","Airbus A330",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A333","airbus-a330","Airbus A330",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A337","airbus-a330","Airbus A330",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A338","airbus-a330","Airbus A330",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A339","airbus-a330","Airbus A330",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A342","airbus-a340","Airbus A340",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"A343","airbus-a340","Airbus A340",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"A345","airbus-a340","Airbus A340",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"A346","airbus-a340","Airbus A340",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"A359","airbus-a350","Airbus A350-900",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A35K","airbus-a350","Airbus A350-1000",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"A388","airbus-a380","Airbus A380",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"AN24","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AN26","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AN30","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AN32","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"A124","antonov-an-124","Antonov An-124",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"A148","antonov-an-148","Antonov An-148 family",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"A158","antonov-an-148","Antonov An-148 family",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"A225","antonov-an-225","Antonov An-225",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"AT43","atr-42","ATR 42",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT44","atr-42","ATR 42",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT45","atr-42","ATR 42",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT46","atr-42","ATR 42",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT72","atr-72","ATR 72",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT73","atr-72","ATR 72",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT75","atr-72","ATR 72",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"AT76","atr-72","ATR 72",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"B461","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"B462","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"B463","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"RJ70","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"RJ85","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"RJ1H","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"B190","beechcraft-1900","Beechcraft 1900",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BE9L","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BE10","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BE20","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BE30","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"B350","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"B701","boeing-707","Boeing 707",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B703","boeing-707","Boeing 707",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B712","boeing-717","Boeing 717",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B721","boeing-727","Boeing 727",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B722","boeing-727","Boeing 727",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B731","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B732","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B733","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B734","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B735","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B736","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B737","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B738","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B739","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B37M","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B38M","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B39M","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B3XM","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B741","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B742","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B743","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B744","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B74R","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B74S","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B748","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"BLCF","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"B752","boeing-757","Boeing 757",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B753","boeing-757","Boeing 757",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"B762","boeing-767","Boeing 767",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B763","boeing-767","Boeing 767",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B764","boeing-767","Boeing 767",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B772","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B773","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B77L","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B77W","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B778","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B779","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B788","boeing-787","Boeing 787",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B789","boeing-787","Boeing 787",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"B78X","boeing-787","Boeing 787",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"CRJ1","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"CRJ2","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"CRJ7","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"CRJ9","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"CRJX","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"C172","cessna-172","Cessna 172",AIRSPACE_AIRCRAFT_SHAPE_LIGHT_PISTON},
    {"C208","cessna-208","Cessna 208 Caravan",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"C408","cessna-408-skycourier","Cessna 408 SkyCourier",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"C500","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C501","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C510","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C525","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C526","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C550","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C551","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C560","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C56X","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C650","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C680","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C700","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C750","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C25A","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C25B","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C25C","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"C68A","citation-latitude","Cessna Citation Latitude",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"CL30","challenger-300-family","Bombardier Challenger 300 family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"CL35","challenger-300-family","Bombardier Challenger 300 family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"SF50","cirrus-vision","Cirrus Vision Jet",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"AJ27","comac-c909","COMAC C909 / ARJ21",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"C919","comac-c919","COMAC C919",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"CONC","concorde","Concorde",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"CVLT","convair-580","Convair 580",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DH8A","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DH8B","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DH8C","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DH8D","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DC10","dc-10","McDonnell Douglas DC-10",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"DC81","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DC82","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DC83","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DC84","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DC85","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DC86","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DC87","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"DHC6","dhc-6-twin-otter","De Havilland Twin Otter",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"D228","dornier-228","Dornier 228",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"D328","dornier-328","Dornier 328",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"J328","dornier-328","Dornier 328",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E120","embraer-120","Embraer EMB 120",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"E170","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E75L","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E75S","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E190","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E195","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E290","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"E295","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"FA7X","falcon-8x","Dassault Falcon 7X / 8X",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"FA8X","falcon-8x","Dassault Falcon 7X / 8X",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"F100","fokker-100","Fokker 100",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"GL7T","global-7500","Bombardier Global 7500",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"GLF5","gulfstream-g550","Gulfstream G550",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"GLF6","gulfstream-g650","Gulfstream G650",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"HDJT","hondajet","HondaJet",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"IL62","ilyushin-il-62","Ilyushin Il-62",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"IL76","ilyushin-il-76","Ilyushin Il-76",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"IL96","ilyushin-il-96","Ilyushin Il-96",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"MC21","irkut-mc-21","Yakovlev MC-21",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"L410","let-l-410","LET L-410",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"L101","lockheed-l-1011","Lockheed L-1011 TriStar",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"MD11","md-11","McDonnell Douglas MD-11",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"MD81","md-80","McDonnell Douglas MD-80 family",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"MD82","md-80","McDonnell Douglas MD-80 family",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"MD83","md-80","McDonnell Douglas MD-80 family",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"MD87","md-80","McDonnell Douglas MD-80 family",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"MD88","md-80","McDonnell Douglas MD-80 family",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"E55P","phenom-300","Embraer Phenom 300",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"PC12","pilatus-pc-12","Pilatus PC-12",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"PC24","pilatus-pc-24","Pilatus PC-24",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"E545","praetor-family","Embraer Praetor family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"E550","praetor-family","Embraer Praetor family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"SF34","saab-340","Saab 340",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"SU95","sukhoi-superjet","Sukhoi Superjet 100",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"SW4","swearingen-metro","Swearingen Metro",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"T154","tupolev-tu-154","Tupolev Tu-154",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"T204","tupolev-tu-204","Tupolev Tu-204 / Tu-214",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"T214","tupolev-tu-204","Tupolev Tu-204 / Tu-214",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"YK42","yakovlev-yak-42","Yakovlev Yak-42",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"AS50","airbus-h125","Airbus H125",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"EC30","airbus-h130","Airbus H130",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"EC35","airbus-h135","Airbus H135",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"BK17","airbus-h145","Airbus H145",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"H160","airbus-h160","Airbus H160",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"B06","bell-206","Bell 206",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"B407","bell-407","Bell 407",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"B429","bell-429","Bell 429",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"KA32","kamov-ka-32","Kamov Ka-32",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"A109","leonardo-aw109","Leonardo AW109",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"A119","leonardo-aw119","Leonardo AW119",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"A139","leonardo-aw139","Leonardo AW139",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"A169","leonardo-aw169","Leonardo AW169",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"H500","md-500","MD 500",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MD90","md-902","MD 902 Explorer",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MI8","mil-mi-8","Mil Mi-8 / Mi-17",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MI17","mil-mi-8","Mil Mi-8 / Mi-17",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"R22","robinson-r22","Robinson R22",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"R44","robinson-r44","Robinson R44",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"S76","sikorsky-s-76","Sikorsky S-76",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"S92","sikorsky-s-92","Sikorsky S-92",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
};

static const model_alias_entry_t model_aliases[] = {    {"AIRBUSA220","BCS1","airbus-a220","Airbus A220",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"A220100","BCS1","airbus-a220","Airbus A220",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"A220300","BCS1","airbus-a220","Airbus A220",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"AIRBUSA300","A30B","airbus-a300","Airbus A300",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"AIRBUSA310","A310","airbus-a310","Airbus A310",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"AIRBUSA318","A318","airbus-a319","Airbus A319",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"AIRBUSA319","A318","airbus-a319","Airbus A319",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"AIRBUSA320","A320","airbus-a320","Airbus A320",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"AIRBUSA321","A321","airbus-a321","Airbus A321",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"AIRBUSA330","A332","airbus-a330","Airbus A330",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"AIRBUSA340","A342","airbus-a340","Airbus A340",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"AIRBUSA350","A359","airbus-a350","Airbus A350",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"AIRBUSA380","A388","airbus-a380","Airbus A380",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"ANTONOVAN24","AN24","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"ANTONOVAN26","AN24","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"ANTONOVAN30","AN24","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"ANTONOVAN32","AN24","antonov-an-24","Antonov An-24 family",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"ANTONOVAN124","A124","antonov-an-124","Antonov An-124",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"ANTONOVAN148","A148","antonov-an-148","Antonov An-148 family",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"ANTONOVAN158","A148","antonov-an-148","Antonov An-148 family",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"ANTONOVAN225","A225","antonov-an-225","Antonov An-225",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"ATR42","AT43","atr-42","ATR 42",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"ATR72","AT72","atr-72","ATR 72",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BAE146","B461","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"AVRORJ","B461","bae-146","BAe 146 / Avro RJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"BEECHCRAFT1900","B190","beechcraft-1900","Beechcraft 1900",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BEECHCRAFTKINGAIR","BE9L","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"KINGAIR","BE9L","beechcraft-king-air","Beechcraft King Air",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BOEING707","B701","boeing-707","Boeing 707",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"BOEING717","B712","boeing-717","Boeing 717",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"BOEING727","B721","boeing-727","Boeing 727",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"BOEING737","B731","boeing-737","Boeing 737",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"BOEING747","B741","boeing-747","Boeing 747",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"BOEING757","B752","boeing-757","Boeing 757",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"BOEING767","B762","boeing-767","Boeing 767",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"BOEING777","B772","boeing-777","Boeing 777",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"BOEING787","B788","boeing-787","Boeing 787",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"DREAMLINER","B788","boeing-787","Boeing 787",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"BOMBARDIERCRJ","CRJ1","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"CANADAIRREGIONALJET","CRJ1","bombardier-crj","Bombardier CRJ",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"CESSNA172","C172","cessna-172","Cessna 172",AIRSPACE_AIRCRAFT_SHAPE_LIGHT_PISTON},
    {"CESSNA208","C208","cessna-208","Cessna 208 Caravan",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"CARAVAN","C208","cessna-208","Cessna 208 Caravan",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"CESSNA408","C408","cessna-408-skycourier","Cessna 408 SkyCourier",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"SKYCOURIER","C408","cessna-408-skycourier","Cessna 408 SkyCourier",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"CESSNACITATION","C500","cessna-citation","Cessna Citation",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"CITATIONLATITUDE","C68A","citation-latitude","Cessna Citation Latitude",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"CHALLENGER300","CL30","challenger-300-family","Bombardier Challenger 300 family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"CHALLENGER350","CL30","challenger-300-family","Bombardier Challenger 300 family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"CIRRUSVISIONJET","SF50","cirrus-vision","Cirrus Vision Jet",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"COMACC909","AJ27","comac-c909","COMAC C909 / ARJ21",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"COMACARJ21","AJ27","comac-c909","COMAC C909 / ARJ21",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"ARJ21","AJ27","comac-c909","COMAC C909 / ARJ21",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"COMACC919","C919","comac-c919","COMAC C919",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"CONCORDE","CONC","concorde","Concorde",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"CONVAIR580","CVLT","convair-580","Convair 580",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DASH8","DH8A","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DEHAVILLANDDASH8","DH8A","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"BOMBARDIERQ400","DH8A","dash-8","De Havilland Dash 8",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"MCDONNELDOUGLASDC10","DC10","dc-10","McDonnell Douglas DC-10",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"DOUGLASDC8","DC81","dc-8","Douglas DC-8",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"TWINOTTER","DHC6","dhc-6-twin-otter","De Havilland Twin Otter",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DEHAVILLANDDHC6","DHC6","dhc-6-twin-otter","De Havilland Twin Otter",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DORNIER228","D228","dornier-228","Dornier 228",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"DORNIER328","D328","dornier-328","Dornier 328",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"EMBRAER120","E120","embraer-120","Embraer EMB 120",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"EMB120","E120","embraer-120","Embraer EMB 120",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"EMBRAEREJET","E170","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"EMBRAER170","E170","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"EMBRAER175","E170","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"EMBRAER190","E170","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"EMBRAER195","E170","embraer-e-jet","Embraer E-Jet",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"DASSAULTFALCON7X","FA7X","falcon-8x","Dassault Falcon 7X / 8X",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"DASSAULTFALCON8X","FA7X","falcon-8x","Dassault Falcon 7X / 8X",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"FOKKER100","F100","fokker-100","Fokker 100",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"GLOBAL7500","GL7T","global-7500","Bombardier Global 7500",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"GULFSTREAMG550","GLF5","gulfstream-g550","Gulfstream G550",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"GULFSTREAMG650","GLF6","gulfstream-g650","Gulfstream G650",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"HONDAJET","HDJT","hondajet","HondaJet",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"ILYUSHINIL62","IL62","ilyushin-il-62","Ilyushin Il-62",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"ILYUSHINIL76","IL76","ilyushin-il-76","Ilyushin Il-76",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"ILYUSHINIL96","IL96","ilyushin-il-96","Ilyushin Il-96",AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY},
    {"IRKUTMC21","MC21","irkut-mc-21","Yakovlev MC-21",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"YAKOVLEVMC21","MC21","irkut-mc-21","Yakovlev MC-21",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"LETL410","L410","let-l-410","LET L-410",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"LOCKHEEDL1011","L101","lockheed-l-1011","Lockheed L-1011 TriStar",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"TRISTAR","L101","lockheed-l-1011","Lockheed L-1011 TriStar",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"MCDONNELDOUGLASMD11","MD11","md-11","McDonnell Douglas MD-11",AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET},
    {"MCDONNELDOUGLASMD80","MD81","md-80","McDonnell Douglas MD-80 family",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"PHENOM300","E55P","phenom-300","Embraer Phenom 300",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"PILATUSPC12","PC12","pilatus-pc-12","Pilatus PC-12",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"PILATUSPC24","PC24","pilatus-pc-24","Pilatus PC-24",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"EMBRAERPRAETOR","E545","praetor-family","Embraer Praetor family",AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET},
    {"SAAB340","SF34","saab-340","Saab 340",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"SUKHOISUPERJET","SU95","sukhoi-superjet","Sukhoi Superjet 100",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"SUPERJET100","SU95","sukhoi-superjet","Sukhoi Superjet 100",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"SWEARINGENMETRO","SW4","swearingen-metro","Swearingen Metro",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"FAIRCHILDMETRO","SW4","swearingen-metro","Swearingen Metro",AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP},
    {"TUPOLEVTU154","T154","tupolev-tu-154","Tupolev Tu-154",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"TUPOLEVTU204","T204","tupolev-tu-204","Tupolev Tu-204 / Tu-214",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"TUPOLEVTU214","T204","tupolev-tu-204","Tupolev Tu-204 / Tu-214",AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET},
    {"YAKOVLEVYAK42","YK42","yakovlev-yak-42","Yakovlev Yak-42",AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET},
    {"AIRBUSH125","AS50","airbus-h125","Airbus H125",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"EUROCOPTERAS350","AS50","airbus-h125","Airbus H125",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AIRBUSH130","EC30","airbus-h130","Airbus H130",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"EUROCOPTEREC130","EC30","airbus-h130","Airbus H130",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AIRBUSH135","EC35","airbus-h135","Airbus H135",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"EUROCOPTEREC135","EC35","airbus-h135","Airbus H135",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AIRBUSH145","BK17","airbus-h145","Airbus H145",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"EUROCOPTEREC145","BK17","airbus-h145","Airbus H145",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AIRBUSH160","H160","airbus-h160","Airbus H160",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"BELL206","B06","bell-206","Bell 206",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"BELL407","B407","bell-407","Bell 407",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"BELL429","B429","bell-429","Bell 429",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"KAMOVKA32","KA32","kamov-ka-32","Kamov Ka-32",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"LEONARDOAW109","A109","leonardo-aw109","Leonardo AW109",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AGUSTAAW109","A109","leonardo-aw109","Leonardo AW109",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"LEONARDOAW119","A119","leonardo-aw119","Leonardo AW119",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AGUSTAAW119","A119","leonardo-aw119","Leonardo AW119",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"LEONARDOAW139","A139","leonardo-aw139","Leonardo AW139",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"AGUSTAAW139","A139","leonardo-aw139","Leonardo AW139",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"LEONARDOAW169","A169","leonardo-aw169","Leonardo AW169",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MCDONNELLDOUGLASMD500","H500","md-500","MD 500",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MDHELICOPTERMD500","H500","md-500","MD 500",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MD902EXPLORER","MD90","md-902","MD 902 Explorer",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MILMI8","MI8","mil-mi-8","Mil Mi-8 / Mi-17",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"MILMI17","MI8","mil-mi-8","Mil Mi-8 / Mi-17",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"ROBINSONR22","R22","robinson-r22","Robinson R22",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"ROBINSONR44","R44","robinson-r44","Robinson R44",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"SIKORSKYS76","S76","sikorsky-s-76","Sikorsky S-76",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
    {"SIKORSKYS92","S92","sikorsky-s-92","Sikorsky S-92",AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER},
};

static size_t normalize_alphanumeric(
    const char *source,
    char *destination,
    size_t destination_size)
{
    if (destination == NULL || destination_size == 0) {
        return 0;
    }
    size_t used = 0;
    for (size_t index = 0;
         source != NULL && source[index] != '\0';
         index++) {
        const unsigned char value = (unsigned char)source[index];
        if (!isalnum(value)) {
            continue;
        }
        if (used + 1 >= destination_size) {
            break;
        }
        destination[used++] = (char)toupper(value);
    }
    destination[used] = '\0';
    return used;
}

static void set_match(
    airspace_aircraft_type_match_t *match,
    const char *code,
    const char *slug,
    const char *label,
    airspace_aircraft_shape_t shape,
    bool exact_code)
{
    if (match == NULL) {
        return;
    }
    match->code = code;
    match->slug = slug;
    match->label = label;
    match->shape = shape;
    match->exact_code = exact_code;
}

bool airspace_aircraft_type_lookup(
    const char *typecode,
    const char *model,
    const char *manufacturer,
    airspace_aircraft_type_match_t *match)
{
    if (match != NULL) {
        memset(match, 0, sizeof(*match));
    }

    char normalized_type[64];
    const size_t type_length = normalize_alphanumeric(
        typecode,
        normalized_type,
        sizeof(normalized_type));
    if (type_length >= 2 && type_length <= 4) {
        for (size_t index = 0;
             index < sizeof(exact_types) / sizeof(exact_types[0]);
             index++) {
            if (strcmp(normalized_type, exact_types[index].code) == 0) {
                set_match(
                    match,
                    exact_types[index].code,
                    exact_types[index].slug,
                    exact_types[index].label,
                    exact_types[index].shape,
                    true);
                return true;
            }
        }
    }

    char normalized_model[96];
    char normalized_manufacturer[64];
    normalize_alphanumeric(model, normalized_model, sizeof(normalized_model));
    normalize_alphanumeric(
        manufacturer,
        normalized_manufacturer,
        sizeof(normalized_manufacturer));

    for (size_t index = 0;
         index < sizeof(model_aliases) / sizeof(model_aliases[0]);
         index++) {
        if ((normalized_type[0] != '\0' &&
             strstr(normalized_type, model_aliases[index].needle) != NULL) ||
            (normalized_model[0] != '\0' &&
             strstr(normalized_model, model_aliases[index].needle) != NULL)) {
            set_match(
                match,
                model_aliases[index].code,
                model_aliases[index].slug,
                model_aliases[index].label,
                model_aliases[index].shape,
                false);
            return true;
        }
    }

    (void)normalized_manufacturer;
    return false;
}

bool airspace_aircraft_is_rotorcraft(
    const char *typecode,
    const char *model,
    const char *manufacturer,
    const char *engine_type,
    int opensky_category)
{
    if (opensky_category == 8) {
        return true;
    }

    airspace_aircraft_type_match_t match;
    if (airspace_aircraft_type_lookup(typecode, model, manufacturer, &match) &&
        match.shape == AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER) {
        return true;
    }

    char normalized_type[16];
    char normalized_model[96];
    char normalized_manufacturer[64];
    char normalized_engine[32];
    normalize_alphanumeric(typecode, normalized_type, sizeof(normalized_type));
    normalize_alphanumeric(model, normalized_model, sizeof(normalized_model));
    normalize_alphanumeric(
        manufacturer,
        normalized_manufacturer,
        sizeof(normalized_manufacturer));
    normalize_alphanumeric(engine_type, normalized_engine, sizeof(normalized_engine));

    // Turboshaft is a much stronger rotorcraft signal than the sometimes-empty
    // OpenSky emitter category. Turboprops are reported separately as TURBOPROP.
    if (strstr(normalized_engine, "TURBOSHAFT") != NULL) {
        return true;
    }

    static const char *rotorcraft_typecodes[] = {
        "AS32","AS55","AS65","ASNG","EC20","EC25","EC45","EC55",
        "H120","H155","H175","B212","B214","B222","B230","B412",
        "B427","B505","B525","R66","A129","A149","A189","S61",
        "S64","S65","S70","H60","H64","UH1","CH47","MI2","MI6",
        "MI10","MI14","MI24","MI26","MI28","MI34","MI38","KA25",
        "KA26","KA27","KA50","KA52","KA60","KA62","H269","H369",
        "MD52","MD60"
    };
    for (size_t index = 0;
         index < sizeof(rotorcraft_typecodes) / sizeof(rotorcraft_typecodes[0]);
         index++) {
        if (strcmp(normalized_type, rotorcraft_typecodes[index]) == 0) {
            return true;
        }
    }

    static const char *model_markers[] = {
        "HELICOPTER","ROTORCRAFT","EUROCOPTER","DOLPHIN","BLACKHAWK",
        "BLACKHAWK","IROQUOIS","APACHE","CHINOOK"
    };
    for (size_t index = 0;
         index < sizeof(model_markers) / sizeof(model_markers[0]);
         index++) {
        if (strstr(normalized_model, model_markers[index]) != NULL) {
            return true;
        }
    }

    static const char *rotorcraft_manufacturers[] = {
        "EUROCOPTER","SIKORSKY","ROBINSON","KAMOV","AGUSTA",
        "MDHELICOPTERS"
    };
    for (size_t index = 0;
         index < sizeof(rotorcraft_manufacturers) /
             sizeof(rotorcraft_manufacturers[0]);
         index++) {
        if (strstr(normalized_manufacturer, rotorcraft_manufacturers[index]) != NULL) {
            return true;
        }
    }

    return (strstr(normalized_model, "AIRBUSH") != NULL) ||
           (strstr(normalized_model, "LEONARDOAW") != NULL);
}

const char *airspace_aircraft_canonical_label(
    const char *typecode,
    const char *model,
    const char *manufacturer)
{
    airspace_aircraft_type_match_t match;
    return airspace_aircraft_type_lookup(
        typecode,
        model,
        manufacturer,
        &match)
        ? match.label
        : NULL;
}

const char *airspace_aircraft_type_source_text(int source)
{
    switch (source) {
    case 1:
        return "ADSBDB";
    case 2:
        return "AirLabs";
    case 3:
        return "FlightAware";
    default:
        return "none";
    }
}
