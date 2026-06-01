# Coding style

Always use Doxygen style comments for documentation. Use backslash `\` notation.

## File heading

/**
 * \file
 * \brief Short description of the file.
 *
 * Detailed description of the file.
 *
 * \author [Author Name] <[Author Email]>
 * \date [Current Date in YYYY-MM-DD format]
 * 
 * \copyright Copyright (c) 2026 Owner
 */

## Class description

/**
 * \class ClassName
 * \brief Short description of the class.
 *
 * Detailed description of the class functionality and usage.
 * 
 * \note This is an optional note for the user of this class.
 * \warning This is an optional warning about potential side effects.
 */
 
## Method / Function description

/**
 * \brief Short description of the method.
 *
 * Detailed description of the method's behavior.
 *
 * \param firstParameter Description of the first input argument.
 * \param secondParameter Description of the second input argument.
 * \return Description of the return value and its meaning.
 * 
 * \retval true If the operation completed successfully.
 * \retval false If an error occurred during execution.
 */

## Member / Global variable description

### Inline description (preferred for simple fields)
int var; //!< Brief description of the variable.

### Heading description (preferred for complex fields or globals)
/**
 * \brief Short description of the variable.
 *
 * Detailed description of the variable's usage, constraints, 
 * or allowed value ranges.
 */
int var;

## Enum description

/**
 * \enum EnumName
 * \brief Short description of the enumeration type.
 *
 * Detailed description of what this group of constants represents.
 */
enum class EnumName {
    ValueFirst,  //!< Brief description for the first value.
    ValueSecond, //!< Brief description for the second value.
    ValueThird   //!< Brief description for the third value.
};

## Namespace description

/**
 * \namespace NamespaceName
 * \brief Short description of the namespace's purpose.
 *
 * Detailed description of the subsystem, module, or logical grouping
 * that this namespace encapsulates.
 */
namespace NamespaceName {
    // Code inside the namespace
}

## Template description

### Template Class
/**
 * \brief Short description of the template class.
 *
 * Detailed description of the class.
 *
 * \tparam T Description of the template type parameter requirements.
 */
template <typename T>
class MyTemplateClass {
    // Class content
};

### Template Method / Function
/**
 * \brief Short description of the template function.
 *
 * Detailed description.
 *
 * \tparam Size Description of the non-type template parameter.
 * \param arrayParam Description of the function argument.
 */
template <std::size_t Size>
void processArray(int (&arrayParam)[Size]);

# Project Architecture & Directory Structure

You must strictly follow the directory layout rules below when organizing or creating new files.

## Namespace to Directory Mapping
* Every **namespace** must have a direct reflection in the physical directory structure.
* Example: Code inside `namespace Subsystem::Module` must be located inside the path `src/subsystem/module/`.

## Class Grouping & Component Directories
To avoid flat, messy directories while preventing over-engineering, use the following rules for grouping:

* **Primary Classes (Components):** Every major, standalone class representing a distinct component must have its own dedicated directory. This directory will contain its header (`.hpp`) and source (`.cpp`) files.
* **Helper Classes and Enums:** Do NOT create separate directories for minor helper classes, utility structures, or enums. 
* **Grouping Rule:** Place helper classes, internal structures, and enums directly inside the directory of the **Primary Class** they support, or inside the parent module directory if shared.

### Example Directory Tree
```text
src/
└── network/                           # Reflects: namespace Network
    ├── protocols/                     # Reflects: namespace Network::Protocols
    │   ├── http_client/               # Dedicated directory for Primary Class
    │   │   ├── http_client.hpp        # Primary Class header
    │   │   ├── http_client.cpp        # Primary Class source
    │   │   ├── http_header_parser.cpp # Helper class source (no separate folder)
    │   │   └── http_status.hpp        # Supporting enum (no separate folder)
    │   └── ssl_socket/                # Another Primary Class directory
    │       ├── ssl_socket.hpp
    │       └── ssl_socket.cpp
    └── network_utils.hpp              # Shared module-level helpers
```

## Source vs Header Layout
* Keep matching `.hpp` and `.cpp` files in the **same directory** corresponding to their class or module. Do not separate them into isolated global `include/` and `src/` roots unless explicitly instructed.

