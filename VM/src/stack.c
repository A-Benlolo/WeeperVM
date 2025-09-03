#include <stdint.h>
#include <stddef.h>
#include "stack.h"

// Initialize the stack
void stack_init(Stack *stack) {
    stack->top = -1;
    stack->free = 0;

    // Link all free frames into a free list
    for (int i = 0; i < STACK_CAPACITY - 1; ++i) {
        stack->frames[i].next = i + 1;
    }
    stack->frames[STACK_CAPACITY - 1].next = -1;
}

// Push data onto the stack
int stack_push(Stack *stack, uint32_t data) {
    if (stack->free == -1) {
        return -1; // Stack full
    }

    int next = stack->free;
    stack->free = stack->frames[next].next;

    stack->frames[next].data = data;
    stack->frames[next].next = stack->top;

    stack->top = next;
    return 0;
}

// Pop data from the stack
uint32_t stack_pop(Stack *stack) {
    if (stack->top == -1)
        return -1; // Stack empty

    int pop_index = stack->top;
    uint32_t data = stack->frames[pop_index].data;

    stack->top = stack->frames[pop_index].next;

    // Recycle frame
    stack->frames[pop_index].next = stack->free;
    stack->free = pop_index;

    return data;
}


// Peek data from the stack
uint32_t stack_peek(Stack *stack) {
    if (stack->top == -1)
        return -1; // Stack empty
    return stack->frames[stack->top].data;
}