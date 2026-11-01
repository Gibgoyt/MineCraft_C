void frame_a(void);
void frame_b(void) {
    frame_a();
}
void frame_a(void) {
    frame_b();
}
