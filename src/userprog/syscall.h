#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define SYSCALLNUM 13

void syscall_init (void);

typedef void (*syscall)(struct intr_frame *); 

void syscall_exit(struct intr_frame *);
void syscall_halt(struct intr_frame *);
void syscall_exec(struct intr_frame *);
void syscall_wait(struct intr_frame *);
void syscall_create(struct intr_frame *);
void syscall_remove(struct intr_frame *);
void syscall_open(struct intr_frame *);
void syscall_filesize(struct intr_frame *);
void syscall_read(struct intr_frame *);
void syscall_write(struct intr_frame *);
void syscall_seek(struct intr_frame *);
void syscall_tell(struct intr_frame *);
void syscall_close(struct intr_frame *);

#endif /* userprog/syscall.h */
