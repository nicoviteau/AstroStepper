# 🌟 AstroStepper

[![Language: C++](https://img.shields.io/badge/Language-C%2B%2B-blue)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Arduino](https://img.shields.io/badge/Platform-Arduino-green)](https://www.arduino.cc/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Bibliothèque de contrôle haute précision pour moteurs pas à pas Arduino (AVR), spécialisée dans le suivi astronomique.

---

## 📋 Table des matières

- [Vue d'ensemble](#vue-densemble)
- [Caractéristiques](#caractéristiques)
- [Installation](#installation)
- [Démarrage rapide](#démarrage-rapide)
- [Utilisation](#utilisation)
- [Spécifications techniques](#spécifications-techniques)
- [Exemples](#exemples)
- [Contribution](#contribution)
- [Licence](#licence)

---

## 🔭 Vue d'ensemble

**AstroStepper** est une bibliothèque Arduino spécialisée pour le contrôle haute précision de moteurs pas à pas dans les applications astronomiques. Elle utilise un contrôle déterministe basé sur une minuterie avec une stabilité exceptionnelle, garantissant un suivi astronomique fluide et précis.

### Cas d'usage
- 📡 Télescopes équatoriaux
- 🔭 Montures de télescope
- ⭐ Suivi stellaire
- 📸 Guidage d'astrophotographie
- 🌙 Applications d'observation du ciel

---

## ✨ Caractéristiques principales

### Performance
- **Génération de pas DDS** : Technologie Direct Digital Synthesis pour une précision extrême
- **Rampe d'accélération** : Démarrage et arrêt fluides sans à-coups
- **Compensation du jeu** : Compense automatiquement le jeu mécanique
- **ISR 16 kHz** : Interruption de service à interruption haute fréquence pour une stabilité maximale

### Fiabilité
- ✅ Contrôle déterministe basé sur minuterie
- ✅ Stabilité élevée pour le suivi long terme
- ✅ Gestion optimale des ressources CPU
- ✅ Support des cartes Arduino AVR

---

## 📦 Installation

Deux méthodes disponibles :

### Méthode 1 : Installation automatique (RECOMMANDÉE)
1. Ouvrez l'Arduino IDE
2. Allez à : **Sketch → Include Library → Add .ZIP Library**
3. Sélectionnez le fichier `AstroStepper.zip`
4. La bibliothèque est prête à l'emploi !

### Méthode 2 : Installation manuelle
1. Extrayez le fichier ZIP d'AstroStepper
2. Copiez le dossier dans `Documents/Arduino/libraries/`
3. Redémarrez l'Arduino IDE

📖 **Pour plus de détails**, consultez le fichier [INSTALL.txt](INSTALL.txt)

---

## 🚀 Démarrage rapide

### Exemple basique

```cpp
#include <AstroStepper.h>

AstroStepper stepper(10, 11);  // Broches STEP et DIR

void setup() {
  stepper.begin();
  stepper.setSpeed(1000);      // Vitesse en pas/seconde
}

void loop() {
  stepper.step();              // Effectue un pas
  delay(1);
}
```

### Charger les exemples
1. **File → Examples → AstroStepper**
2. Sélectionnez `basic_tracking` pour commencer
3. Compilez et chargez sur votre carte

---

## 📚 Utilisation

Pour une documentation complète sur l'utilisation, les fonctions disponibles et les paramètres de configuration, consultez le fichier [USAGE.txt](USAGE.txt).

### Principales fonctions

| Fonction | Description |
|----------|-------------|
| `begin()` | Initialise la bibliothèque |
| `setSpeed(speed)` | Configure la vitesse en pas/seconde |
| `setAcceleration(accel)` | Configure l'accélération |
| `step()` | Exécute un pas |
| `setDirection(dir)` | Définit la direction |
| `setBacklash(value)` | Configure la compensation du jeu |

---

## 🔧 Spécifications techniques

### Compatibilité matérielle
- **Plateformes** : Arduino AVR (Uno, Mega, Nano, etc.)
- **Fréquence ISR** : 16 kHz
- **Précision** : Microseconde

### Configuration requise
- **Arduino IDE** : Version 1.8.0 ou supérieure
- **Carte Arduino** : Compatible AVR
- **Mémoire** : Minimal

### Performance
- Fréquence d'interruption élevée (16 kHz) pour une stabilité optimale
- Générateur de pas DDS pour une précision extrême
- Compensation automatique du jeu mécanique

---

## 💡 Exemples

Plusieurs exemples sont inclus dans la bibliothèque :

- **basic_tracking** : Suivi astronomique simple
- *D'autres exemples peuvent être disponibles*

Consultez le dossier `examples/` pour plus d'informations.

---

## 🤝 Contribution

Les contributions sont les bienvenues ! Si vous trouvez un bug ou avez une suggestion :

1. Ouvrez une [issue](https://github.com/nicoviteau/AstroStepper/issues)
2. Créez une pull request avec vos améliorations
3. Décrivez clairement les changements apportés

---

## 📄 Licence

Ce projet est sous licence MIT. Consultez le fichier [LICENSE](LICENSE) pour plus de détails.

---

## ⚠️ Disclaimer

Cette bibliothèque est destinée à un usage éducatif et amateur. Pour les applications critiques ou commerciales, des tests et validations supplémentaires peuvent être nécessaires.

---

## 📞 Support

- 📖 Consultez la [documentation](USAGE.txt)
- 📋 Vérifiez les [exemples](examples/)
- 🐛 Signalez les bugs via les [issues GitHub](https://github.com/nicoviteau/AstroStepper/issues)

---

**Dernière mise à jour** : Mai 2026  
**Mainteneur** : [@nicoviteau](https://github.com/nicoviteau)

