# Patch Notes — Correction erreur "No such gate: socketOut"

## Erreur initiale
```
No such gate or gate vector: 'socketOut' -- in module (DomainNode)
StigmergyNetwork.domain[0] (id=9), during network initialization
```

## Cause racine

Conflit d'architecture en deux endroits :

### 1. `simulations/omnetpp.ini` (cause principale)
Le fichier configurait des sous-modules INET sur `domain[*]` :
```ini
*.domain[*].wlan[0].typename = "Ieee80211Interface"   ← INVALIDE
*.domain[*].mobility.typename = "StationaryMobility"  ← INVALIDE
*.radioMedium.typename = "Ieee80211ScalarRadioMedium"  ← INVALIDE
```
OMNeT++ cherchait alors la gate `socketOut` (propre à `AdhocHost` INET)
sur `DomainNode` qui est un `simple` module sans cette gate.

### 2. `src/StigmergyNetwork.ned` (cause secondaire)
Importait des modules INET (`Ieee80211ScalarRadioMedium`, `IntegratedCanvasVisualizer`,
`PhysicalEnvironment`, `Ipv4NetworkConfigurator`) jamais utilisés → warnings et
risque de conflit de namespace.

## Architecture réelle (correcte)

`DomainNode` est un **simple module pur OMNeT++** qui communique via `sendDirect()` :
```cpp
// DomainNode.cc ligne 690
sendDirect(msg, dest, "directIn");
```
La seule gate déclarée dans `DomainNode.ned` est :
```ned
gates:
    input directIn @directIn;
```
→ Pas de wlan, pas de socket UDP, pas de couche réseau INET.

## Fichiers modifiés

| Fichier | Modification |
|---------|-------------|
| `simulations/omnetpp.ini` | Suppression de toutes les configs `*.domain[*].wlan[*]`, `*.domain[*].mobility.*`, `*.radioMedium.*`, `*.visualizer.*` |
| `src/StigmergyNetwork.ned` | Suppression des imports INET inutiles, nettoyage des submodules |

## Fichiers NON modifiés
- `src/DomainNode.cc` ✓
- `src/DomainNode.ned` ✓
- `src/DomainNode.h` ✓
- `src/PheromoneField.*` ✓
- `src/AdversaryModel.*` ✓
- `src/MetricsCollector.*` ✓

## Commande de test après patch
```bash
bash run.sh Debug_Quick
```
Doit démarrer sans erreur et produire des logs `[DomainNode]` dans le terminal.

## Impact sur les expériences E3 (réseau dégradé)
Les pertes réseau (`packetLoss`, `jammingProb`) sont maintenant modélisées
**abstraitement** via `PheromoneField.jammingProb` et le paramètre
`*.packetLoss` du réseau — sans couche physique INET réelle.
Cela est cohérent avec l'architecture `sendDirect()` choisie.
