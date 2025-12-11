// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include "custom_parsers/csv/csv_segmenter.h"
#include "custom_parsers/shared_components/numeric_graphs.h"
#include "custom_parsers/shared_components/string_graphs.h"
#include "custom_parsers/tests/DebugIntrospectionHooks.h"
#include "openzl/codecs/zl_parse_int.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/logging.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_reflection.h"

namespace openzl::tests {
namespace {

enum CsvSuccessorIdx : size_t {
    GENERIC_STRING  = 0, // COMPRESS_GENERIC
    GENERIC_NUMERIC = 1, // field LZ
    NUMERIC_TOKEN   = 2, // null-aware -> parse-int -> tokenize
    FIXED_WIDTH_DEC = 3, // TODO
    STRING_TOKEN    = 4, // string_tokenize
    LONG_DATE       = 5, // TODO
    FLAG_TOKEN      = 6, // same as token1
    TOKEN1          = 7, // separate -> {entropy, constant}
    TOKEN2          = 8, // separate -> {convert to token2 -> entropy, constant}
    DELTA           = 9,
};

class Config {
   public:
    virtual ~Config() = default;
    // Create and register the parsing graph
    virtual ZL_GraphID registerParsingGraph(
            ZL_Compressor* compressor,
            const std::vector<ZL_GraphID>& successors) = 0;
};

class TPC_H_lineitem : public Config {
   public:
    ZL_GraphID registerParsingGraph(
            ZL_Compressor* compressor,
            const std::vector<ZL_GraphID>& successors) override
    {
        const std::vector<CsvSuccessorIdx> info = {
            GENERIC_NUMERIC, // ORDERKEY
            NUMERIC_TOKEN,   // PARTKEY
            NUMERIC_TOKEN,   // SUPPKEY
            NUMERIC_TOKEN,   // LINENUMBER
            GENERIC_NUMERIC, // QUANTITY
            FIXED_WIDTH_DEC, // EXTENDEDPRICE
            FIXED_WIDTH_DEC, // DISCOUNT
            FIXED_WIDTH_DEC, // TAX
            STRING_TOKEN,    // RETURNFLAG
            STRING_TOKEN,    // LINESTATUS
            LONG_DATE,       // SHIPDATE
            LONG_DATE,       // COMMITDATE
            LONG_DATE,       // RECEIPTDATE
            STRING_TOKEN,    // SHIPINSTRUCT
            STRING_TOKEN,    // SHIPMODE
            GENERIC_STRING,  // COMMENT
        };

        std::map<size_t, std::vector<int>> clusterMemberTags;
        std::vector<ZL_ClusteringConfig_Cluster> clusters;
        for (size_t i = 0; i < info.size(); ++i) {
            clusterMemberTags[info[i]].push_back((int)i);
        }
        for (auto& [k, v] : clusterMemberTags) {
            ZL_ClusteringConfig_Cluster cluster;
            cluster.typeSuccessor = {
                .type         = ZL_Type_string,
                .eltWidth     = 0u,
                .successorIdx = k,
            };
            cluster.memberTags   = v.data();
            cluster.nbMemberTags = v.size();
            clusters.push_back(cluster);
        }
        std::vector<ZL_ClusteringConfig_TypeSuccessor> defaultSuccessors = {
            { .type               = ZL_Type_string,
              .eltWidth           = 0,
              .successorIdx       = 1,
              .clusteringCodecIdx = 3 },
        };
        ZL_ClusteringConfig clusteringConfig = {
            .clusters       = clusters.data(),
            .nbClusters     = clusters.size(),
            .typeDefaults   = defaultSuccessors.data(),
            .nbTypeDefaults = defaultSuccessors.size(),
        };

        auto clusteringGraph = ZL_Clustering_registerGraph(
                compressor,
                &clusteringConfig,
                successors.data(),
                successors.size());
        return ZL_RES_value(ZL_CsvSegmenter_registerSegmenterNoChunks(
                compressor, true, '|', false, clusteringGraph));
    }
};

template <bool UseNullAware>
class PsamH01 : public Config {
   public:
    ZL_GraphID registerParsingGraph(
            ZL_Compressor* compressor,
            const std::vector<ZL_GraphID>& successors) override
    {
        const std::vector<CsvSuccessorIdx> info = {
            STRING_TOKEN,    // RT
            GENERIC_STRING,  // SERIALNO
            GENERIC_STRING,  // DIVISION
            STRING_TOKEN,    // PUMA
            STRING_TOKEN,    // REGION
            STRING_TOKEN,    // STATE
            NUMERIC_TOKEN,   // ADJHSG
            NUMERIC_TOKEN,   // ADJINC
            GENERIC_NUMERIC, // WGTP
            NUMERIC_TOKEN,   // NP
            STRING_TOKEN,    // TYPEHUGQ
            // unit variables
            NUMERIC_TOKEN,   // ACCESSINET
            NUMERIC_TOKEN,   // ACR
            NUMERIC_TOKEN,   // AGS
            NUMERIC_TOKEN,   // BATH
            NUMERIC_TOKEN,   // BDSP
            STRING_TOKEN,    // BLD
            NUMERIC_TOKEN,   // BROADBND
            NUMERIC_TOKEN,   // COMPOTHX
            GENERIC_NUMERIC, // CONP
            NUMERIC_TOKEN,   // DIALUP
            NUMERIC_TOKEN,   // ELEFP
            GENERIC_NUMERIC, // ELEP
            NUMERIC_TOKEN,   // FS
            NUMERIC_TOKEN,   // FULFP
            GENERIC_NUMERIC, // FULP
            NUMERIC_TOKEN,   // GASFP
            GENERIC_NUMERIC, // GASP
            NUMERIC_TOKEN,   // HFL
            NUMERIC_TOKEN,   // HISPEED
            NUMERIC_TOKEN,   // HOTWAT
            GENERIC_NUMERIC, // INSP
            NUMERIC_TOKEN,   // LAPTOP
            GENERIC_NUMERIC, // MHP
            NUMERIC_TOKEN,   // MRGI
            GENERIC_NUMERIC, // MRGP
            NUMERIC_TOKEN,   // MRGT
            NUMERIC_TOKEN,   // MRGX
            NUMERIC_TOKEN,   // OTHSVCEX
            NUMERIC_TOKEN,   // REFR
            NUMERIC_TOKEN,   // RMSP
            NUMERIC_TOKEN,   // RNTM
            GENERIC_NUMERIC, // RNTP
            NUMERIC_TOKEN,   // RWAT
            NUMERIC_TOKEN,   // RWATPR
            NUMERIC_TOKEN,   // SATELLITE
            NUMERIC_TOKEN,   // SINK
            NUMERIC_TOKEN,   // SMARTPHONE
            GENERIC_NUMERIC, // SMP
            NUMERIC_TOKEN,   // STOV
            NUMERIC_TOKEN,   // TABLET
            NUMERIC_TOKEN,   // TEL
            NUMERIC_TOKEN,   // TEN
            NUMERIC_TOKEN,   // VACS
            GENERIC_NUMERIC, // VALP
            NUMERIC_TOKEN,   // VEH
            NUMERIC_TOKEN,   // WATFP
            NUMERIC_TOKEN,   // WATP
            NUMERIC_TOKEN,   // YRBLT
            NUMERIC_TOKEN,   // CPLT
            GENERIC_NUMERIC, // FINCP
            NUMERIC_TOKEN,   // FPARC
            GENERIC_NUMERIC, // GRNTP
            GENERIC_NUMERIC, // GRPIP TODO(check)
            NUMERIC_TOKEN,   // HHL
            STRING_TOKEN,    // HHLANP TODO(check)
            NUMERIC_TOKEN,   // HHLDRAGEP
            STRING_TOKEN,    // HHLDRHISP
            NUMERIC_TOKEN,   // HHLDRRAC1P
            NUMERIC_TOKEN,   // HHT
            STRING_TOKEN,    // HHT2
            GENERIC_NUMERIC, // HINCP
            NUMERIC_TOKEN,   // HUGCL
            NUMERIC_TOKEN,   // HUPAC
            NUMERIC_TOKEN,   // HUPAOC
            NUMERIC_TOKEN,   // HUPARC
            NUMERIC_TOKEN,   // KIT
            NUMERIC_TOKEN,   // LNGI
            NUMERIC_TOKEN,   // MULTG
            NUMERIC_TOKEN,   // MV
            NUMERIC_TOKEN,   // NOC
            NUMERIC_TOKEN,   // NPF
            NUMERIC_TOKEN,   // NPP
            NUMERIC_TOKEN,   // NR
            NUMERIC_TOKEN,   // NRC
            NUMERIC_TOKEN,   // OCPIP
            NUMERIC_TOKEN,   // PARTNER
            NUMERIC_TOKEN,   // PLM
            NUMERIC_TOKEN,   // PLMPRP
            NUMERIC_TOKEN,   // PSF
            NUMERIC_TOKEN,   // R18
            NUMERIC_TOKEN,   // R60
            NUMERIC_TOKEN,   // R65
            NUMERIC_TOKEN,   // RESMODE
            GENERIC_NUMERIC, // SMOCP
            NUMERIC_TOKEN,   // SMX
            NUMERIC_TOKEN,   // SRNT
            NUMERIC_TOKEN,   // SVAL
            GENERIC_NUMERIC, // TAXAMT
            NUMERIC_TOKEN,   // WIF
            STRING_TOKEN,    // WKEXREL
            STRING_TOKEN,    // WORKSTAT
            // allocation flags
            STRING_TOKEN, // FACCESSP
            STRING_TOKEN, // FACRP
            STRING_TOKEN, // FAGSP
            STRING_TOKEN, // FBATHP
            STRING_TOKEN, // FBDSP
            STRING_TOKEN, // FBLDP
            STRING_TOKEN, // FBROADBNDP
            STRING_TOKEN, // FCOMPOTHXP
            STRING_TOKEN, // FCONP
            STRING_TOKEN, // FDIALUPP
            STRING_TOKEN, // FELEP
            STRING_TOKEN, // FFINCP
            STRING_TOKEN, // FFSP
            STRING_TOKEN, // FFULP
            STRING_TOKEN, // FGASP
            STRING_TOKEN, // FGRNTP
            STRING_TOKEN, // FHFLP
            STRING_TOKEN, // FHINCP
            STRING_TOKEN, // FHISPEEDP
            STRING_TOKEN, // FHOTWATP
            STRING_TOKEN, // FINSP
            STRING_TOKEN, // FKITP
            STRING_TOKEN, // FLAPTOPP
            STRING_TOKEN, // FMHP
            STRING_TOKEN, // FMRGIP
            STRING_TOKEN, // FMRGP
            STRING_TOKEN, // FMRGTP
            STRING_TOKEN, // FMRGXP
            STRING_TOKEN, // FMVP
            STRING_TOKEN, // FOTHSVCEXP
            STRING_TOKEN, // FPLMP
            STRING_TOKEN, // FPLMPRP
            STRING_TOKEN, // FREFRP
            STRING_TOKEN, // FRMSP
            STRING_TOKEN, // FRNTMP
            STRING_TOKEN, // FRNTP
            STRING_TOKEN, // FRWATP
            STRING_TOKEN, // FRWATPRP
            STRING_TOKEN, // FSATELLITEP
            STRING_TOKEN, // FSINKP
            STRING_TOKEN, // FSMARTPHONP
            STRING_TOKEN, // FSMOCP
            STRING_TOKEN, // FSMP
            STRING_TOKEN, // FSMXHP
            STRING_TOKEN, // FSMXSP
            STRING_TOKEN, // FSTOVP
            STRING_TOKEN, // FTABLETP
            STRING_TOKEN, // FTAXP
            STRING_TOKEN, // FTELP
            STRING_TOKEN, // FTENP
            STRING_TOKEN, // FVACSP
            STRING_TOKEN, // FVALP
            STRING_TOKEN, // FVEHP
            STRING_TOKEN, // FWATP
            STRING_TOKEN, // FYRBLTP
            // replicate weights
            NUMERIC_TOKEN, // WGTP1
            NUMERIC_TOKEN, // WGTP2
            NUMERIC_TOKEN, // WGTP3
            NUMERIC_TOKEN, // WGTP4
            NUMERIC_TOKEN, // WGTP5
            NUMERIC_TOKEN, // WGTP6
            NUMERIC_TOKEN, // WGTP7
            NUMERIC_TOKEN, // WGTP8
            NUMERIC_TOKEN, // WGTP9
            NUMERIC_TOKEN, // WGTP10
            NUMERIC_TOKEN, // WGTP11
            NUMERIC_TOKEN, // WGTP12
            NUMERIC_TOKEN, // WGTP13
            NUMERIC_TOKEN, // WGTP14
            NUMERIC_TOKEN, // WGTP15
            NUMERIC_TOKEN, // WGTP16
            NUMERIC_TOKEN, // WGTP17
            NUMERIC_TOKEN, // WGTP18
            NUMERIC_TOKEN, // WGTP19
            NUMERIC_TOKEN, // WGTP20
            NUMERIC_TOKEN, // WGTP21
            NUMERIC_TOKEN, // WGTP22
            NUMERIC_TOKEN, // WGTP23
            NUMERIC_TOKEN, // WGTP24
            NUMERIC_TOKEN, // WGTP25
            NUMERIC_TOKEN, // WGTP26
            NUMERIC_TOKEN, // WGTP27
            NUMERIC_TOKEN, // WGTP28
            NUMERIC_TOKEN, // WGTP29
            NUMERIC_TOKEN, // WGTP30
            NUMERIC_TOKEN, // WGTP31
            NUMERIC_TOKEN, // WGTP32
            NUMERIC_TOKEN, // WGTP33
            NUMERIC_TOKEN, // WGTP34
            NUMERIC_TOKEN, // WGTP35
            NUMERIC_TOKEN, // WGTP36
            NUMERIC_TOKEN, // WGTP37
            NUMERIC_TOKEN, // WGTP38
            NUMERIC_TOKEN, // WGTP39
            NUMERIC_TOKEN, // WGTP40
            NUMERIC_TOKEN, // WGTP41
            NUMERIC_TOKEN, // WGTP42
            NUMERIC_TOKEN, // WGTP43
            NUMERIC_TOKEN, // WGTP44
            NUMERIC_TOKEN, // WGTP45
            NUMERIC_TOKEN, // WGTP46
            NUMERIC_TOKEN, // WGTP47
            NUMERIC_TOKEN, // WGTP48
            NUMERIC_TOKEN, // WGTP49
            NUMERIC_TOKEN, // WGTP50
            NUMERIC_TOKEN, // WGTP51
            NUMERIC_TOKEN, // WGTP52
            NUMERIC_TOKEN, // WGTP53
            NUMERIC_TOKEN, // WGTP54
            NUMERIC_TOKEN, // WGTP55
            NUMERIC_TOKEN, // WGTP56
            NUMERIC_TOKEN, // WGTP57
            NUMERIC_TOKEN, // WGTP58
            NUMERIC_TOKEN, // WGTP59
            NUMERIC_TOKEN, // WGTP60
            NUMERIC_TOKEN, // WGTP61
            NUMERIC_TOKEN, // WGTP62
            NUMERIC_TOKEN, // WGTP63
            NUMERIC_TOKEN, // WGTP64
            NUMERIC_TOKEN, // WGTP65
            NUMERIC_TOKEN, // WGTP66
            NUMERIC_TOKEN, // WGTP67
            NUMERIC_TOKEN, // WGTP68
            NUMERIC_TOKEN, // WGTP69
            NUMERIC_TOKEN, // WGTP70
            NUMERIC_TOKEN, // WGTP71
            NUMERIC_TOKEN, // WGTP72
            NUMERIC_TOKEN, // WGTP73
            NUMERIC_TOKEN, // WGTP74
            NUMERIC_TOKEN, // WGTP75
            NUMERIC_TOKEN, // WGTP76
            NUMERIC_TOKEN, // WGTP77
            NUMERIC_TOKEN, // WGTP78
            NUMERIC_TOKEN, // WGTP79
            NUMERIC_TOKEN, // WGTP80
        };
        std::map<size_t, std::vector<int>> clusterMemberTags;
        std::vector<ZL_ClusteringConfig_Cluster> clusters;
        for (size_t i = 0; i < info.size(); ++i) {
            clusterMemberTags[info[i]].push_back((int)i);
        }
        for (auto& [k, v] : clusterMemberTags) {
            ZL_ClusteringConfig_Cluster cluster;
            cluster.typeSuccessor = {
                .type         = ZL_Type_string,
                .eltWidth     = 0u,
                .successorIdx = k,
            };
            cluster.memberTags   = v.data();
            cluster.nbMemberTags = v.size();
            clusters.push_back(cluster);
        }
        std::vector<ZL_ClusteringConfig_TypeSuccessor> defaultSuccessors = {
            { .type               = ZL_Type_string,
              .eltWidth           = 0,
              .successorIdx       = 1,
              .clusteringCodecIdx = 3 },
        };
        ZL_ClusteringConfig clusteringConfig = {
            .clusters       = clusters.data(),
            .nbClusters     = clusters.size(),
            .typeDefaults   = defaultSuccessors.data(),
            .nbTypeDefaults = defaultSuccessors.size(),
        };

        auto clusteringGraph = ZL_Clustering_registerGraph(
                compressor,
                &clusteringConfig,
                successors.data(),
                successors.size());
        auto ee = ZL_RES_value(ZL_CsvSegmenter_registerSegmenterNoChunks(
                compressor, true, ',', UseNullAware, clusteringGraph));
        if (ee.gid == ZL_GRAPH_ILLEGAL.gid) {
            std::cerr << "illegal graph!" << std::endl;
            exit(1);
        }
        return ee;
    }
};

class PPMF_Unit : public Config {
   public:
    ZL_GraphID registerParsingGraph(
            ZL_Compressor* compressor,
            const std::vector<ZL_GraphID>& successors) override
    {
        const std::vector<CsvSuccessorIdx> info = {
            TOKEN1,       // RTYPE
            STRING_TOKEN, // GQTYPE
            TOKEN1,       // TEN
            TOKEN1,       // VACS
            TOKEN1,       // HHSIZE
            TOKEN1,       // HHT
            TOKEN2,       // HHT2
            TOKEN1,       // CPLT
            TOKEN1,       // UPART
            TOKEN1,       // MULTG
            TOKEN1,       // THHLDRAGE
            TOKEN1,       // THHSPAN
            TOKEN2,       // THHRACE
            TOKEN1,       // PAOC
            FLAG_TOKEN,   // TP18
            FLAG_TOKEN,   // TP60
            FLAG_TOKEN,   // TP65
            FLAG_TOKEN,   // TP75
            TOKEN1,       // PAC
            TOKEN1,       // HHSEX
            TOKEN1,       // TENSHORT
            TOKEN1,       // HH_STATUS
            // Geographies
            TOKEN2,          // TABBLKST
            STRING_TOKEN,    // TABBLKCOU
            STRING_TOKEN,    // TABTRACTCE
            STRING_TOKEN,    // TABBLK
            TOKEN1,          // TABBLKGRPCE
            STRING_TOKEN,    // AIANNHCE
            STRING_TOKEN,    // AIANNHFP
            STRING_TOKEN,    // AIANNHNS double-check
            TOKEN1,          // AIHHTLI
            STRING_TOKEN,    // ANRCFP
            STRING_TOKEN,    // ANRCNS
            GENERIC_NUMERIC, // AREALAND
            GENERIC_NUMERIC, // AREAWATER
            GENERIC_NUMERIC, // AREAWATERCSTL
            GENERIC_NUMERIC, // AREAWATERGRLK
            GENERIC_NUMERIC, // AREAWATERINLD
            GENERIC_NUMERIC, // AREAWATERTSEA
            STRING_TOKEN,    // CBSAFP
            TOKEN2,          // CD116FP
            STRING_TOKEN,    // CNECTAFP
            STRING_TOKEN,    // CONCITFP
            STRING_TOKEN,    // CONCITNS
            TOKEN1,          // COUNTYFS
            STRING_TOKEN,    // COUNTYNS
            STRING_TOKEN,    // COUSUBFP
            TOKEN1,          // COUSUBFS
            STRING_TOKEN,    // COUSUBNS
            STRING_TOKEN,    // CSAFP
            TOKEN1,          // DIVISIONCE
            STRING_TOKEN,    // ESTATEFP
            STRING_TOKEN,    // ESTATENS
            GENERIC_STRING,  // INTPTLAT or maybe token?
            GENERIC_STRING,  // INTPTLON or maybe token?
            TOKEN1,          // LWBLKTYP
            TOKEN1,          // MEMI
            STRING_TOKEN,    // METDIVFP
            STRING_TOKEN,    // NECTADIVFP
            STRING_TOKEN,    // NECTAFP
            TOKEN1,          // NMEMI
            TOKEN1,          // PCICBSA
            TOKEN1,          // PCINECTA
            STRING_TOKEN,    // PLACEFP
            TOKEN1,          // PLACEFS
            STRING_TOKEN,    // PLACENS
            STRING_TOKEN,    // PUMA
            TOKEN1,          // REGIONCE
            STRING_TOKEN,    // SDELMLEA
            STRING_TOKEN,    // SDSECLEA
            STRING_TOKEN,    // SDUNILEA
            STRING_TOKEN,    // SLDLST
            STRING_TOKEN,    // SLDUST
            STRING_TOKEN,    // STATENS
            STRING_TOKEN,    // SUBMCDFP
            STRING_TOKEN,    // SUBMCDNS
            TOKEN1,          // TBLKGRPCE
            STRING_TOKEN,    // TRIBALSUBCE
            STRING_TOKEN,    // TRIBALSUBFP
            STRING_TOKEN,    // TRIBALSUBNS
            STRING_TOKEN,    // TTRACTCE
            STRING_TOKEN,    // UACE
            TOKEN1,          // UATYP
            STRING_TOKEN,    // UGACE
            TOKEN1,          // UR
            STRING_TOKEN,    // VTDST
            STRING_TOKEN,    // ZCTA5CE
        };
        std::map<size_t, std::vector<int>> clusterMemberTags;
        std::vector<ZL_ClusteringConfig_Cluster> clusters;
        for (size_t i = 0; i < info.size(); ++i) {
            clusterMemberTags[info[i]].push_back((int)i);
        }
        for (auto& [k, v] : clusterMemberTags) {
            ZL_ClusteringConfig_Cluster cluster;
            cluster.typeSuccessor = {
                .type         = ZL_Type_string,
                .eltWidth     = 0u,
                .successorIdx = k,
            };
            cluster.memberTags   = v.data();
            cluster.nbMemberTags = v.size();
            clusters.push_back(cluster);
        }
        std::vector<ZL_ClusteringConfig_TypeSuccessor> defaultSuccessors = {
            { .type               = ZL_Type_string,
              .eltWidth           = 0,
              .successorIdx       = 1,
              .clusteringCodecIdx = 3 },
        };
        ZL_ClusteringConfig clusteringConfig = {
            .clusters       = clusters.data(),
            .nbClusters     = clusters.size(),
            .typeDefaults   = defaultSuccessors.data(),
            .nbTypeDefaults = defaultSuccessors.size(),
        };

        auto clusteringGraph = ZL_Clustering_registerGraph(
                compressor,
                &clusteringConfig,
                successors.data(),
                successors.size());
        auto ee = ZL_RES_value(ZL_CsvSegmenter_registerSegmenterNoChunks(
                compressor, true, ',', false, clusteringGraph));
        if (ee.gid == ZL_GRAPH_ILLEGAL.gid) {
            std::cerr << "illegal graph!" << std::endl;
            exit(1);
        }
        return ee;
    }
};

class PPMF_Person : public Config {
   public:
    ZL_GraphID registerParsingGraph(
            ZL_Compressor* compressor,
            const std::vector<ZL_GraphID>& successors) override
    {
        // EPNUM,RTYPE,GQTYPE,RELSHIP,QSEX,QAGE,CENHISP,CENRACE,LIVE_ALONE,NUMRACE,PGQSHRT,GQTYPE_PL,VOTING_AGE,TABBLKST,TABBLKCOU,TABTRACTCE,TABBLK,TABBLKGRPCE,AIANNHCE,AIANNHFP,AIANNHNS,AIHHTLI,ANRCFP,ANRCNS,AREALAND,AREAWATER,AREAWATERCSTL,AREAWATERGRLK,AREAWATERINLD,AREAWATERTSEA,CBSAFP,CD116FP,CNECTAFP,CONCITFP,CONCITNS,COUNTYFS,COUNTYNS,COUSUBFP,COUSUBFS,COUSUBNS,CSAFP,DIVISIONCE,ESTATEFP,ESTATENS,INTPTLAT,INTPTLON,LWBLKTYP,MEMI,METDIVFP,NECTADIVFP,NECTAFP,NMEMI,PCICBSA,PCINECTA,PLACEFP,PLACEFS,PLACENS,PUMA,REGIONCE,SDELMLEA,SDSECLEA,SDUNILEA,SLDLST,SLDUST,STATENS,SUBMCDFP,SUBMCDNS,TBLKGRPCE,TRIBALSUBCE,TRIBALSUBFP,TRIBALSUBNS,TTRACTCE,UACE,UATYP,UGACE,UR,VTDST,ZCTA5CE
        const std::vector<CsvSuccessorIdx> info = {
            DELTA,         // EPNUM
            TOKEN1,        // RTYPE
            STRING_TOKEN,  // GQTYPE
            TOKEN2,        // RELSHIP
            TOKEN1,        // QSEX
            NUMERIC_TOKEN, // QAGE
            TOKEN1,        // CENHISP
            TOKEN2,        // CENRACE
            TOKEN1,        // LIVE_ALONE
            TOKEN1,        // NUMRACE
            TOKEN1,        // PGQSHRT
            TOKEN1,        // GQTYPE_PL
            TOKEN1,        // VOTING_AGE
            // Geographies
            TOKEN2,          // TABBLKST
            STRING_TOKEN,    // TABBLKCOU
            STRING_TOKEN,    // TABTRACTCE
            STRING_TOKEN,    // TABBLK
            TOKEN1,          // TABBLKGRPCE
            STRING_TOKEN,    // AIANNHCE
            STRING_TOKEN,    // AIANNHFP
            STRING_TOKEN,    // AIANNHNS double-check
            TOKEN1,          // AIHHTLI
            STRING_TOKEN,    // ANRCFP
            STRING_TOKEN,    // ANRCNS
            GENERIC_NUMERIC, // AREALAND
            GENERIC_NUMERIC, // AREAWATER
            GENERIC_NUMERIC, // AREAWATERCSTL
            GENERIC_NUMERIC, // AREAWATERGRLK
            GENERIC_NUMERIC, // AREAWATERINLD
            GENERIC_NUMERIC, // AREAWATERTSEA
            STRING_TOKEN,    // CBSAFP
            TOKEN2,          // CD116FP
            STRING_TOKEN,    // CNECTAFP
            STRING_TOKEN,    // CONCITFP
            STRING_TOKEN,    // CONCITNS
            TOKEN1,          // COUNTYFS
            STRING_TOKEN,    // COUNTYNS
            STRING_TOKEN,    // COUSUBFP
            TOKEN1,          // COUSUBFS
            STRING_TOKEN,    // COUSUBNS
            STRING_TOKEN,    // CSAFP
            TOKEN1,          // DIVISIONCE
            STRING_TOKEN,    // ESTATEFP
            STRING_TOKEN,    // ESTATENS
            GENERIC_STRING,  // INTPTLAT or maybe token?
            GENERIC_STRING,  // INTPTLON or maybe token?
            TOKEN1,          // LWBLKTYP
            TOKEN1,          // MEMI
            STRING_TOKEN,    // METDIVFP
            STRING_TOKEN,    // NECTADIVFP
            STRING_TOKEN,    // NECTAFP
            TOKEN1,          // NMEMI
            TOKEN1,          // PCICBSA
            TOKEN1,          // PCINECTA
            STRING_TOKEN,    // PLACEFP
            TOKEN1,          // PLACEFS
            STRING_TOKEN,    // PLACENS
            STRING_TOKEN,    // PUMA
            TOKEN1,          // REGIONCE
            STRING_TOKEN,    // SDELMLEA
            STRING_TOKEN,    // SDSECLEA
            STRING_TOKEN,    // SDUNILEA
            STRING_TOKEN,    // SLDLST
            STRING_TOKEN,    // SLDUST
            STRING_TOKEN,    // STATENS
            STRING_TOKEN,    // SUBMCDFP
            STRING_TOKEN,    // SUBMCDNS
            TOKEN1,          // TBLKGRPCE
            STRING_TOKEN,    // TRIBALSUBCE
            STRING_TOKEN,    // TRIBALSUBFP
            STRING_TOKEN,    // TRIBALSUBNS
            STRING_TOKEN,    // TTRACTCE
            STRING_TOKEN,    // UACE
            TOKEN1,          // UATYP
            STRING_TOKEN,    // UGACE
            TOKEN1,          // UR
            STRING_TOKEN,    // VTDST
            STRING_TOKEN,    // ZCTA5CE
        };
        std::map<size_t, std::vector<int>> clusterMemberTags;
        std::vector<ZL_ClusteringConfig_Cluster> clusters;
        for (size_t i = 0; i < info.size(); ++i) {
            clusterMemberTags[info[i]].push_back((int)i);
        }
        for (auto& [k, v] : clusterMemberTags) {
            ZL_ClusteringConfig_Cluster cluster;
            cluster.typeSuccessor = {
                .type         = ZL_Type_string,
                .eltWidth     = 0u,
                .successorIdx = k,
            };
            cluster.memberTags   = v.data();
            cluster.nbMemberTags = v.size();
            clusters.push_back(cluster);
        }
        std::vector<ZL_ClusteringConfig_TypeSuccessor> defaultSuccessors = {
            { .type               = ZL_Type_string,
              .eltWidth           = 0,
              .successorIdx       = 1,
              .clusteringCodecIdx = 3 },
        };
        ZL_ClusteringConfig clusteringConfig = {
            .clusters       = clusters.data(),
            .nbClusters     = clusters.size(),
            .typeDefaults   = defaultSuccessors.data(),
            .nbTypeDefaults = defaultSuccessors.size(),
        };

        auto clusteringGraph = ZL_Clustering_registerGraph(
                compressor,
                &clusteringConfig,
                successors.data(),
                successors.size());
        auto ee = ZL_RES_value(ZL_CsvSegmenter_registerSegmenterNoChunks(
                compressor, true, ',', false, clusteringGraph));
        if (ee.gid == ZL_GRAPH_ILLEGAL.gid) {
            std::cerr << "illegal graph!" << std::endl;
            exit(1);
        }
        return ee;
    }
};

template <int CLevel>
class Zstd : public Config {
   public:
    ZL_GraphID registerParsingGraph(
            ZL_Compressor* compressor,
            const std::vector<ZL_GraphID>& successors) override
    {
        return ZL_Compressor_registerZstdGraph_withLevel(compressor, CLevel);
    }
};

class TestCsv {
   public:
    ZL_Report run(const std::string& csvFile);
    void TearDown()
    {
        ZL_Compressor_free(compressor_);
    }

    void SetUp()
    {
        compressor_ = ZL_Compressor_create();

        const auto flz1 =
                ZL_Compressor_registerFieldLZGraph_withLevel(compressor_, 1);
        const auto flz = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_, ZL_NODE_PARSE_INT, flz1);

        std::array<ZL_GraphID, 3> hpCustomGraphs = {
            ZL_GRAPH_ENTROPY,
            ZL_GRAPH_COMPRESS_GENERIC,
            flz,
        };

        // null-aware dispatch
        const auto nullAwareFlz =
                openzl::custom_parsers::registerNullAwareDispatch(
                        compressor_, "nullAwareFlz", hpCustomGraphs);

        const auto numericTokenize =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        compressor_,
                        ZL_NODE_PARSE_INT,
                        openzl::custom_parsers::
                                ZL_Compressor_registerTokenizeSorted(
                                        compressor_));
        hpCustomGraphs[2] = numericTokenize;
        const auto nullAwareNumericTokenize =
                openzl::custom_parsers::registerNullAwareDispatch(
                        compressor_,
                        "nullAwareNumericTokenize",
                        hpCustomGraphs);
        const auto stringTokenize =
                openzl::custom_parsers::ZL_Compressor_registerStringTokenize(
                        compressor_);

        std::array<ZL_GraphID, 2> tokenizeSuccs = { ZL_GRAPH_ZSTD,
                                                    ZL_GRAPH_ENTROPY };
        const auto tokenizeGid = ZL_Compressor_registerStaticGraph_fromNode(
                compressor_,
                ZL_NODE_TOKENIZE,
                tokenizeSuccs.data(),
                tokenizeSuccs.size());
        std::array<ZL_GraphID, 2> size1StringSuccs = { tokenizeGid,
                                                       ZL_GRAPH_CONSTANT };
        const auto token1 = ZL_Compressor_registerStaticGraph_fromNode(
                compressor_,
                ZL_NODE_SEPARATE_STRING_COMPONENTS,
                size1StringSuccs.data(),
                size1StringSuccs.size());

        std::array<ZL_GraphID, 2> size2StringSuccs = {
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor_, ZL_NODE_INTERPRET_AS_LE16, tokenizeGid),
            ZL_GRAPH_CONSTANT
        };
        const auto token2 = ZL_Compressor_registerStaticGraph_fromNode(
                compressor_,
                ZL_NODE_SEPARATE_STRING_COMPONENTS,
                size2StringSuccs.data(),
                size2StringSuccs.size());

        std::array<ZL_NodeID, 2> deltaPipe = { ZL_NODE_PARSE_INT,
                                               ZL_NODE_DELTA_INT };
        const auto delta =
                ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
                        compressor_,
                        deltaPipe.data(),
                        deltaPipe.size(),
                        ZL_GRAPH_CONSTANT);

        successors_[GENERIC_STRING]  = ZL_GRAPH_COMPRESS_GENERIC;
        successors_[GENERIC_NUMERIC] = nullAwareFlz;
        successors_[NUMERIC_TOKEN]   = nullAwareNumericTokenize;
        successors_[FIXED_WIDTH_DEC] = ZL_GRAPH_COMPRESS_GENERIC; // TODO
        successors_[STRING_TOKEN]    = stringTokenize;
        successors_[LONG_DATE]       = stringTokenize; // TODO
        successors_[FLAG_TOKEN]      = token1;
        successors_[TOKEN1]          = token1;
        successors_[TOKEN2]          = token2;
        successors_[DELTA]           = delta;
    }

   private:
    ZL_Compressor* compressor_{};
    std::vector<ZL_GraphID> successors_{ 10 };
};

ZL_Report TestCsv::run(const std::string& csvFile)
{
    SetUp();
    ZL_g_logLevel = ZL_LOG_LVL_DEBUG;

    auto cctx = ZL_CCtx_create();

    if (0) {
        DebugIntrospectionHooks hooks{};
        ZL_REQUIRE_SUCCESS(
                ZL_CCtx_attachIntrospectionHooks(cctx, hooks.getRawHooks()));
    }

    std::ifstream file{ csvFile };
    std::stringstream sstrm;
    sstrm << file.rdbuf();
    std::string src = sstrm.str();

    std::ofstream statsFile;
    statsFile.open(
            "/data/users/csv/csv_stats.txt",
            std::ios_base::out | std::ios_base::app);
    // schema:
    // ZL_CSpeed,ZL_Ratio,ZL_DSpeed,Zstd_CSpeed,Zstd_Ratio,Zstd_DSpeed

    std::vector<ZL_GraphID> toTest;
    // toTest.push_back(
    //         PsamH01<false>().registerParsingGraph(compressor_, successors_));
    // toTest.push_back(
    //         PsamH01<true>().registerParsingGraph(compressor_, successors_));
    toTest.push_back(
            PPMF_Unit().registerParsingGraph(compressor_, successors_));
    toTest.push_back(Zstd<6>().registerParsingGraph(compressor_, successors_));

    for (auto clevel : { 1, 6 }) {
        for (ZL_GraphID csvParserGid : toTest) {
            ZL_RET_R_IF_ERR(ZL_CCtx_setParameter(
                    cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
            ZL_RET_R_IF_ERR(ZL_CCtx_refCompressor(cctx, compressor_));
            ZL_RET_R_IF_ERR(ZL_CCtx_setParameter(
                    cctx, ZL_CParam_compressionLevel, clevel));
            auto gssr = ZL_Compressor_selectStartingGraphID(
                    compressor_, csvParserGid);
            ZL_RET_R_IF_ERR(gssr);
            std::string dst(src.size() * 2, 0);

            auto start = std::chrono::high_resolution_clock::now();
            auto r     = ZL_CCtx_compress(
                    cctx, dst.data(), dst.size(), src.data(), src.size());
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            std::cerr << "CSV custom Compression time: " << elapsed.count()
                      << " (" << src.size() / elapsed.count() / 1e6 << "mbps)"
                      << std::endl;
            if (ZL_isError(r)) {
                std::cerr << ZL_CCtx_getErrorContextString(cctx, r)
                          << std::endl;
                return r;
            }
            auto cr = (float)src.size() / (float)ZL_validResult(r);
            std::cerr << "Parsed Compression ratio: " << cr << std::endl;
            statsFile << src.size() / elapsed.count() / 1e6 << "," << cr << ",";

            dst.resize(ZL_validResult(r));
            // roundtrip speed
            std::string regen(src.size(), 0);
            start = std::chrono::high_resolution_clock::now();
            r     = ZL_decompress(
                    regen.data(), regen.size(), dst.data(), dst.size());
            end     = std::chrono::high_resolution_clock::now();
            elapsed = end - start;
            std::cerr << "CSV custom Decompression time: " << elapsed.count()
                      << " (" << src.size() / elapsed.count() / 1e6 << "mbps)"
                      << std::endl;
            if (regen != src) {
                std::cerr << "Roundtrip failed" << std::endl;
                std::ofstream regenFile{ "regen.csv" };
                regenFile << regen;
                regenFile.close();
                std::ofstream srcFile{ "src.csv" };
                srcFile << src;
                srcFile.close();
                return ZL_returnSuccess();
            }
            statsFile << src.size() / elapsed.count() / 1e6 << ",";
        }
    }

    statsFile << csvFile << std::endl;
    statsFile.flush();
    ZL_CCtx_free(cctx);
    TearDown();
    return ZL_returnSuccess();
}

} // namespace
} // namespace openzl::tests

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <csv file>" << std::endl;
        return 1;
    }
    std::string csvFile = argv[1];
    auto report         = openzl::tests::TestCsv().run(csvFile);
    if (ZL_isError(report)) {
        std::cerr << ZL_ErrorCode_toString(ZL_errorCode(report));
    }
    return 0;
}
