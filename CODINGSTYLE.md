# OSv Coding Style
This document describe OSv coding style.

## 1. Indentation and layout
1.1 We use 4 spaces for indentation, no tabs.

1.2 switch statements, put the case with same indentation as the switch
```
    switch(op) {
    case 1:
            i++;
            break;
    case 2:
    case 3:
           i *= 2;
           break;
    default:
           break;
```

1.3 Avoid multiple statements on the same line:
```
    i++; j++;
```

1.4 Line length should not exceed 80 characters.

## 2. Spaces
2.1 Use spaces around binary and ternary operators.
```
   a = a + 3;
   if (a == 1 || b < 2)
   a += 1;
   a = 1 + 2 * 3;
   a = b < 1 ? b : 1;
```

2.2 Do not use spaces around unary operators.
```
   a = -2;
   s = *p;
   for (int i = 3; i < 10; ++i)
```

2.3 Do not use spaces between a function and its parameters, or a
template and its paramters.
```
   sqrt(2.0)
   std::vector<object*>
```

## 3. Braces
3.1 Always use curly braces for if statement, even if it is a one line if.

3.2 When a brace-delimited block is part of a statement (e.g., if, for,
switch, WITH_LOCK, etc.), separate the open brace from the statement
with a single space - not with a newline.
```
    if (a == 3) {
        ....
    }
````

3.2 In inline method, you can use the open braces at the same line of the method.
```
    int get_age() {
        return age;
    }
```

3.3 In longer method,  the opening brace should be at the beginning of the line.
```
    void clear()
    {
       .....
    }
```

## 4. Naming Convention
4.1 Use all lower snake_case names

## 5. Commenting
5.1 Use the // C++ comment style for normal comment
5.2 When documenting a namespace, class, method or function using Doxygen, use /** */ comments.

## 6. Macros, Enums and RTL
6.1 Avoid Macros when a method would do. Prefer enum and constant to macro.
6.2 Prefer "enum class" to "enum".

6.3 Macro names and enum label should be capitalized. For "enum class",
non-capitalized values are fine.

## 7. Functions
7.1 When declaring or defining a function taking no arguments in C++ code,
avoid the unnecessary "void" as an argument list.

This "void" was only necessary in C to maintain backward-compatibility with
pre-1989 prototype-less declarations, but was never needed in C++ code.
For example, write:

```C++
void abort() {
```

and not:

```C++
void abort(void) {
```

7.2 Put no space between function name and the argument list. For example:

```C++
double sqrt(double d) {
```

7.3 Avoid parantheses around return value

"return" is not a function - it doesn't need parantheses. For example:

```C++
return 0;
return a + b;
```
