struct ReusableSocketMessage : SocketMessage {
	struct CountedMessage {
	  capnp::MallocMessageBuilder message_builder;
	  std::atomic<std::uint32_t> read_count;
	};
  struct message_ptr {
	  CountedMessage& message_;
	  message_ptr(CountedMessage& message) : message_(message) {
		  message_.read_count++;
	  }
	  ~message_ptr() {
		  if (!--message_.read_count) {
			  delete &message_;
		  }
	  }
  };

  CountedMessage* message_builder;
  std::queue<std::function<void(capnp::MallocMessageBuilder&)>> write_queue_;
  std::queue<std::function<void(message_ptr&)>> read_queue_;
  bool writing;

  template<typename TCallback>
  void ReadMessage(TCallback&& callback) const {
	  message_ptr message(*this);
	  if (writing) {
		  read_queue_.push(callback);
	  } else {
		  callback(message);
	  }
  }

  void WriteMessage(TCallback&& callback) {
	  if (writing) {
		  write_queue_.push(callback);
	  } else {
		  if (message_builder->read_count) {
			  message_builder = new CountedMessage();
		  }
		  writing = true;
		  callback(*message_builder);
		  while (!write_queue_.empty()) {
			  write_queue_.pop()(*message_builder);
		  }
		  writing = false;
		  while (!read_queue_.empty()) {
			  read_queue_.pop()(*message_builder);
		  }
	  }
  }

  capnp::MallocMessageBuilder& GetMessageBuilder() {
	if (message_builder.use_count() != 1) {
	  message_builder = std::make_shared<capnp::MallocMessageBuilder>(*message_builder);
	}
	return *message_builder;
  }
};
