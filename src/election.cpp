#include <election.h>

void Election::exit_handler(int sn, siginfo_t* t, void* ctx) {
    if (running_as == RunningType::AsManager) {
        m->exit_handler(sn, t, ctx);
    } else {
        h->exit_handler(sn, t, ctx);
    }
}

void Election::switch_manager() {
    std::vector<KnownHost> copy = h->get_hosts();
    h->exit_handler(0, nullptr, nullptr);
    
    running_as = RunningType::AsManager;
    m = std::make_unique<Manager>(Manager());
    char hostname[BUFFER_SIZE];
    gethostname(hostname, BUFFER_SIZE);
    
    for (auto c : copy) 
        if(c.name != hostname) m->add_host(c);

    pthread_create(&this->t_manager, NULL, Election::manager, this);
}

void Election::switch_host() {
    m->exit_handler(0, nullptr, nullptr);

    running_as = RunningType::AsHost;
    h = std::make_unique<Host>(Host());

    if (m->b_should_try_election)
        h->b_should_bootstrap_election = true;

    pthread_create(&this->t_host, NULL, Election::host, this);
}

void Election::init() {
    /* starts executing as host */
    h = std::make_unique<Host>(Host());
    pthread_create(&this->t_host, NULL, Election::host, this);
    pthread_create(&this->t_check, NULL, Election::check, this);

    pthread_join(this->t_check, NULL);
}

void *Election::host(void *ctx) {
    Election *e = ((Election *) ctx);
    e->h->init();
    return 0;
}

void *Election::manager(void *ctx) {
    Election *e = ((Election *) ctx);
    e->m->init();
    return 0;
}

void *Election::check(void *ctx) {
    Election *e = ((Election *) ctx);

    while(1) {
        if (e->running_as == RunningType::AsHost) {
            if (e->h->b_should_exit_election) 
                break;
            if (e->h->b_should_switch_manager)
                e->switch_manager();
        } else {
            if (e->m->b_should_exit_election)
                break;
            if (e->m->b_should_switch_host)
                e->switch_host();
        }
        usleep(e->sleep_check);
    }
    return 0;
}