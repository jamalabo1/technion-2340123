    // note that if I put this after the while cond, this creates 3 different stages for the reader
    // either it's cond blocked, or log_mutex blocked or reading, the problem is a reader can get preempted after it gives up the wc mutex
    // then a writer comes, increments the writer cond, aquires the log sem, reader 1 gets blocked. but now reader 2 does not even wait for log_sem so it starts reading
    // while writer is writing, BAD!.
    // If I put this after rc_mutex, it means it cannot be the case that after we checked no writer is there then a sneaky writer comes and aquires the lock
    // so we endup at the log_mutex, this ensures no body actually gets stuck at the log_mutex. and the writers do not do anything while readers are checking
    // I can implement another binary gate sempahore that the first writer holds and readers check it after the cond, but this also relies on fairness anyway
    // since we are saying ok, you have been waken up from the cond, you can try to continue, so you try to open the gate
    // if you are successfull you enter. what is the case when not? it's when a first writer came and tried to close the gate. the os only give it the option
    // through fairness, because It could be the case that N readers tried and the writer came, so what now? we are waiting for our turn to aquire the gate(as writers)
    // but it's essentially the same as waiting for the wc_mutex to incremenet, so why bother?
    // so it will just let those readers who came before it finish



// why do this if after the lock? so that it's always the readers wake up after I gave up the lock
// since there is no meaning for them to wake up just to find me holding the lock