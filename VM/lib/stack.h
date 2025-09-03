#define STACK_CAPACITY 128


#ifndef STACK_H
#define STACK_H

typedef struct StackFrame {
    uint32_t data;
    int next; // index of the next frame, or -1 if end
} StackFrame;


typedef struct {
    StackFrame frames[STACK_CAPACITY];
    int top;        // index of the top element, or -1 if empty
    int free;       // index of the first free slot
} Stack;


void stack_init(Stack *stack);
int stack_push(Stack *stack, uint32_t data);
uint32_t stack_pop(Stack *stack);
uint32_t stack_peek(Stack *stack);

#endif // STACK_H