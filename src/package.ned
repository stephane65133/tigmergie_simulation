// =============================================================================
//  package.ned  —  src/
//
//  Déclaration du package racine du projet Stigmergie Multi-Domaine.
//
//  Ce fichier est OBLIGATOIRE dans OMNeT++ pour que l'IDE et le compilateur
//  NED reconnaissent le namespace "stigmergie" et résolvent les imports.
//
//  Convention de nommage du projet :
//    Package NED   : stigmergie
//    Namespace C++ : stigmergie   (à déclarer dans chaque .h/.cc)
//    Répertoire    : src/
//
//  Modules déclarés dans ce package (src/) :
//    stigmergie.DomainNode         — nœud domaine, logique stigmergique
//    stigmergie.PheromoneField     — champ phéromonal global partagé
//    stigmergie.StigmergyNetwork   — réseau principal (mesh ad-hoc)
//    stigmergie.StigmergyNetworkWired — variante filaire (debug/baseline)
//    stigmergie.MetricsCollector   — collecte et export CSV des métriques
//
//  Dépendances externes (NED path à configurer dans .oppfeatures ou IDE) :
//    inet   — INET Framework 4.x    (chemin : $INET_ROOT/src)
//
//  Structure du projet :
//    stigmergie_omnetpp/
//    ├── src/
//    │   ├── package.ned              ← CE FICHIER
//    │   ├── StigmergyNetwork.ned
//    │   ├── DomainNode.ned
//    │   ├── DomainNode.h / .cc
//    │   ├── PheromoneField.h / .cc
//    │   ├── PheromoneField.ned
//    │   └── MetricsCollector.h / .cc
//    ├── simulations/
//    │   ├── package.ned              ← déclaration simulations/
//    │   └── omnetpp.ini
//    ├── results/
//    │   ├── raw/
//    │   ├── processed/
//    │   └── figures/
//    └── Makefile  (généré par opp_makemake)
//
//  Configuration IDE OMNeT++ (Window → Preferences → OMNeT++ → NED Source Folders) :
//    Ajouter :  stigmergie_omnetpp/src
//               $INET_ROOT/src
//
//  Compilation :
//    cd stigmergie_omnetpp
//    opp_makemake -f --deep -o stigmergie -KINET_PROJ=<chemin_inet> \
//        -DINET_IMPORT -I$INET_PROJ/src -L$INET_PROJ/src -lINET
//    make -j$(nproc)
//
//  Lancement :
//    opp_run -n src:simulations:$INET_ROOT/src \
//            -l $INET_ROOT/src/INET \
//            -c E1_Proposed -r 0..29 \
//            simulations/omnetpp.ini
// =============================================================================

package stigmergie;
