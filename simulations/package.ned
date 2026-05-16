// =============================================================================
//  package.ned  —  simulations/
//
//  Déclaration du sous-package pour les fichiers de simulation.
//
//  Ce fichier est nécessaire pour que OMNeT++ trouve les réseaux référencés
//  dans omnetpp.ini (network = stigmergie.StigmergyNetwork).
//
//  Le répertoire simulations/ contient :
//    omnetpp.ini   — configurations des 5 expériences E1→E5 + 5 baselines
//    package.ned   — CE FICHIER
//
//  Le réseau principal reste déclaré dans src/ (package stigmergie).
//  Ce package simulations/ ne redéclare aucun module — il sert uniquement
//  à satisfaire l'exigence OMNeT++ d'un package.ned dans tout répertoire
//  contenant des fichiers NED ou des configs qui y font référence.
//
//  NED Source Folders à déclarer dans l'IDE :
//    stigmergie_omnetpp/src
//    stigmergie_omnetpp/simulations
//    $INET_ROOT/src
// =============================================================================

package stigmergie.simulations;
