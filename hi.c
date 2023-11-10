        for (int i = 0; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &read_fds)){
            if (i == listener)
                {
                    // handle new connections
                    // accept from channels
                    socklen_t addr_size = sizeof their_addr;
                    int accept_fd = accept(listener, (struct sockaddr *)&their_addr, &addr_size);
                    fcntl(accept_fd, F_SETFL, O_NONBLOCK);
                    fprintf(stderr, "accepted, fd: %d\n", accept_fd);

                    if (accept_fd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {

                        FD_SET(accept_fd, &master_fds); // add to read_fds set
                        if (accept_fd > fdmax)
                        {
                            fdmax = accept_fd;
                        }
                    }

                } 
                
                
                else {
                    if ((nbytes = recv(i, &received_buffer, sizeof(received_buffer), 0)) > 0) {
                        // fprintf(stderr, "Start unserialization\n");
                        struct message m;
                        ntohl(received_buffer);
                        unserialize_message(received_buffer, sizeof(received_buffer), &m);
                        // fprintf(stderr, "Unserialized message\n");
                        // fprintf(stderr, "message number: %d\n", m.m);
                        message_type_determination(m, i);
                        free(m.membership_list);
                        // send_heartbeat();
                    } else {
                            perror("recv");
                            
                            close(i);
                            FD_CLR(i, &master_fds); // remove from read_fds set
                    }
                }
            }
        }