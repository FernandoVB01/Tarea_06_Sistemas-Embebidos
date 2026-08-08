# Cómo levantar el proyecto — GitHub y VS Code

Guía de los pasos que tienes que ejecutar tú, porque requieren tus
credenciales y tu máquina.

---

## Parte A — Subir a GitHub

El repositorio ya viene **inicializado y con dos commits hechos**. Solo falta
conectarlo con GitHub y empujarlo.

### A.1 Crear el repositorio vacío en GitHub

En https://github.com/new:

| Campo | Valor sugerido |
|---|---|
| Repository name | `Tarea6-Ahorro-Energia-ESP32` |
| Visibility | **Public** (para que el profesor pueda abrirlo) |
| Initialize with README | ❌ **NO marcar** — ya tenemos uno |
| .gitignore / license | ❌ ninguno — ya está incluido |

Marcar cualquiera de esas casillas crea un commit inicial que choca con el
historial local y obliga a resolver un conflicto innecesario.

### A.2 Conectar y empujar

Desde la carpeta del proyecto, en PowerShell:

```powershell
cd C:\ruta\a\Tarea6_Ahorro_Energia

git remote add origin https://github.com/TU_USUARIO/Tarea6-Ahorro-Energia-ESP32.git
git branch -M main
git push -u origin main
```

Sustituye `TU_USUARIO` por tu usuario real de GitHub.

### A.3 Autenticación

GitHub ya no acepta contraseña de cuenta por HTTPS. Dos opciones:

- **GitHub CLI** (lo más cómodo): instala `gh`, corre `gh auth login` una vez
  y las credenciales quedan configuradas para siempre.
- **Personal Access Token**: Settings → Developer settings → Personal access
  tokens → Tokens (classic) → Generate new token, con permiso `repo`. Cuando
  git pida contraseña, pega el token.

### A.4 Verificar

```powershell
git log --oneline
git remote -v
```

Debe aparecer `origin` apuntando a tu repositorio y los dos commits.

### A.5 Commits siguientes

```powershell
git add -A
git commit -m "Agregar capturas de la simulacion"
git push
```

---

## Parte B — Abrir en VS Code

### B.1 Extensiones necesarias

`Ctrl+Shift+X` e instala:

| Extensión | Para qué |
|---|---|
| **PlatformIO IDE** | compilar y grabar |
| **Wokwi for VS Code** | simular |
| **C/C++** (Microsoft) | autocompletado |

PlatformIO descarga el toolchain de ESP-IDF la primera vez. Son varios cientos
de MB, así que la primera compilación tarda bastante — es normal.

### B.2 Abrir el proyecto

**File → Open Folder** y selecciona la carpeta `Tarea6_Ahorro_Energia`
(la que contiene `platformio.ini`, no la carpeta padre). PlatformIO detecta
el proyecto automáticamente.

### B.3 Compilar y grabar

En la barra inferior:

| Icono | Acción |
|---|---|
| ✓ | Build |
| → | Upload |
| 🔌 | Serial Monitor |

O por terminal:

```powershell
pio run                    # compilar
pio run --target upload    # grabar
pio device monitor         # monitor serial
```

### B.4 Simular en Wokwi

1. `pio run` primero — Wokwi necesita el `.bin` y el `.elf` ya generados.
2. `Ctrl+Shift+P` → **Wokwi: Start Simulator**
3. La primera vez pide una licencia gratuita: `Ctrl+Shift+P` →
   **Wokwi: Request a New License**.

---

## Parte C — Sobre Proteus

**Proteus no puede simular este proyecto.** No existe modelo del ESP32 para
Proteus: su biblioteca cubre AVR, PIC, ARM y 8051, pero no el Xtensa LX6.
Las "librerías ESP32 para Proteus" que circulan por internet son maquetas
gráficas que no ejecutan firmware real.

Aunque existieran, Proteus tampoco modela consumo en modos de sueño, que es
justamente lo que esta tarea busca demostrar.

Por eso la guía especifica **Wokwi**, que sí ejecuta el binario real del ESP32.
Proteus te seguirá sirviendo para tus proyectos con ATmega328P y PIC16F887.

---

## Parte D — Checklist de entrega

### Repositorio
- [x] Código fuente organizado
- [x] README.md explicativo
- [x] Instrucciones de compilación y ejecución
- [ ] Subido a GitHub y **público**

### Documento PDF
- [x] Descripción del ejercicio *(en el README)*
- [x] Explicación del funcionamiento *(en el README)*
- [ ] Capturas de la simulación en Wokwi
- [ ] Evidencia en hardware real (fotos del multímetro)
- [x] Análisis del comportamiento *(§8 del README)*
- [x] Tres conclusiones y dos recomendaciones *(§10 del README)*
- [ ] Enlace al repositorio

### Video en YouTube
- [ ] Demostración del funcionamiento
- [ ] Explicación general del código
- [ ] Visibilidad **pública** o **no listado** (si queda privado el profesor no puede verlo)

---

## Parte E — Guion sugerido para el video (5–7 min)

1. **Introducción (30 s)** — qué modos se implementan y qué se busca demostrar.
2. **Recorrido del código (2 min)** — los tres módulos; detenerse en
   `energia.c`, que es donde está el contenido de la tarea.
3. **Simulación en Wokwi (2 min)** — mostrar el ciclo completo; presionar el
   botón para despertar antes de tiempo y señalar el cambio en la causa de
   despertar.
4. **Hardware real (2 min)** — el multímetro en cada fase. Explicar por qué la
   lectura en deep sleep no baja a 10 µA. **Este es el punto más valioso del
   video**: demuestra que entendiste la diferencia entre el datasheet del chip
   y el comportamiento de una placa de desarrollo.
5. **Conclusiones (30 s)**.
