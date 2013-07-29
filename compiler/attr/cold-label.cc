
void f()
{
    goto lbl;
    lbl:    __attribute__((cold));
}




