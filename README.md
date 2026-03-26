# 🧩 Service Controller Emulator

A lightweight C++ service controller emulator that simulates lifecycle operations such as **create, start, stop, delete, query, and failure handling** for services. This project is useful for testing, prototyping, or learning how service control systems behave without relying on actual OS-level service managers.


---


## 📦 Features

- ✅ Service lifecycle management:
  - Create
  - Start
  - Stop
  - Delete
- 🔍 Query service status and descriptions
- ⚠️ Failure simulation and handling
- 🧪 Test harness (`sctest.cpp`)
- 🛠 Utility helpers for shared functionality
- ⚙️ Configurable behavior


---


## 🗂 Project Structure
├── config.* # Configuration handling

├── create.* # Service creation logic

├── delete.* # Service deletion logic

├── start.* # Start service

├── stop.* # Stop service

├── query.* # Query service status

├── qc.* # Query configuration

├── qdescription.* # Query service description

├── failure.* # Failure handling logic

├── qfailure.* # Query failure settings

├── utils.* # Shared utilities

├── sctest.cpp # Test / entry point



---


## ⚙️ Architecture Overview

The emulator mimics a simplified **Service Control Manager (SCM)** by modularizing each operation into separate components:

- **Command Modules** (`create`, `start`, `stop`, etc.)  
  Each module implements a specific service control command.

- **Query Modules** (`query`, `qc`, `qdescription`, `qfailure`)  
  Provide introspection into service state and configuration.

- **Failure Handling**  
  Simulates service failure scenarios and recovery behaviors.

- **Utilities Layer (`utils`)**  
  Common helpers used across modules (logging, parsing, state handling).

- **Configuration Layer (`config`)**  
  Defines how services are stored, initialized, and managed.


---


## 🚀 Getting Started

### Prerequisites

- C++ compiler (GCC, Clang, or MSVC)
- C++11 or later



---


## 🧠 Key Concepts

### Service State Model

Services typically move through states such as:

- CREATED
- RUNNING
- STOPPED
- FAILED

### Failure Simulation

The emulator allows you to:

- Inject failures
- Query failure conditions
- Simulate recovery logic

### Separation of Concerns

Each `.cpp/.h` pair isolates a single responsibility, making it easy to:

- Extend functionality
- Add new commands
- Swap implementations


---


## 🛠 Extending the Emulator

You can extend this project by:

### ➕ Adding New Commands
- Create new `.h/.cpp` files  
- Follow existing module patterns  
- Integrate into `sctest.cpp`  

### 🧩 Enhancing State Management
- Add persistence (file or in-memory DB)  
- Track service dependencies  

### 🔐 Adding Permissions
- Simulate admin/user roles  
- Restrict operations  

### 🌐 Adding CLI Interface

Convert `sctest.cpp` into a real CLI tool:


./sc_emulator start MyService

./sc_emulator query MyService


---


## 🧪 Testing

Currently:

- `sctest.cpp` acts as a manual/integration test  

You can improve this by:

- Adding unit tests (e.g., using GoogleTest)  
- Mocking service state  
- Automating scenarios  


---


## ⚠️ Limitations

- ❌ Not integrated with OS-level service managers  
- ❌ No real process spawning  
- ❌ Likely in-memory only (no persistence unless added)  
- ❌ Limited error handling depending on implementation  


---


## 📌 Use Cases

- Learning how service controllers work  
- Prototyping service orchestration logic  
- Testing failure/recovery flows  
- Interview or educational projects  


---


## 👤 Author

*(Aarya)*
