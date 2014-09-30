/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2012, 2013, 2014.

    This file is part of Sortix.

    Sortix is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version.

    Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along with
    Sortix. If not, see <http://www.gnu.org/licenses/>.

    fs/user.cpp
    User-space filesystem.

*******************************************************************************/

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <timespec.h>

#include <sortix/dirent.h>
#include <sortix/fcntl.h>
#include <sortix/stat.h>
#include <sortix/termios.h>
#include <sortix/timespec.h>

#include <fsmarshall-msg.h>

#include <sortix/kernel/descriptor.h>
#include <sortix/kernel/inode.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/mtable.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/syscall.h>
#include <sortix/kernel/vnode.h>

namespace Sortix {

namespace UserFS {

class ChannelDirection;
class Channel;
class ChannelNode;
class Server;
class ServerNode;
class Unode;

class ChannelDirection
{
public:
	ChannelDirection();
	~ChannelDirection();
	size_t Send(ioctx_t* ctx, const void* ptr, size_t least, size_t max);
	size_t Recv(ioctx_t* ctx, void* ptr, size_t least, size_t max);
	void SendClose();
	void RecvClose();

private:
	static const size_t BUFFER_SIZE = 8192;
	uint8_t buffer[BUFFER_SIZE];
	size_t buffer_used;
	size_t buffer_offset;
	kthread_mutex_t transfer_lock;
	kthread_cond_t not_empty;
	kthread_cond_t not_full;
	bool still_reading;
	bool still_writing;

};

class Channel
{
public:
	Channel();
	~Channel();

public:
	bool KernelSend(ioctx_t* ctx, const void* ptr, size_t count)
	{
		return KernelSend(ctx, ptr, count, count) == count;
	}
	size_t KernelSend(ioctx_t* ctx, const void* ptr, size_t least, size_t max);
	bool KernelRecv(ioctx_t* ctx, void* ptr, size_t count)
	{
		return KernelRecv(ctx, ptr, count, count) == count;
	}
	size_t KernelRecv(ioctx_t* ctx, void* ptr, size_t least, size_t max);
	void KernelClose();

public:
	bool UserSend(ioctx_t* ctx, const void* ptr, size_t count)
	{
		return UserSend(ctx, ptr, count, count) == count;
	}
	size_t UserSend(ioctx_t* ctx, const void* ptr, size_t least, size_t max);
	bool UserRecv(ioctx_t* ctx, void* ptr, size_t count)
	{
		return UserRecv(ctx, ptr, count, count) == count;
	}
	size_t UserRecv(ioctx_t* ctx, void* ptr, size_t least, size_t max);
	void UserClose();

private:
	kthread_mutex_t kernel_lock;
	kthread_mutex_t user_lock;
	kthread_mutex_t destruction_lock;
	ChannelDirection from_kernel;
	ChannelDirection from_user;
	bool kernel_closed;
	bool user_closed;

};

class ChannelNode : public AbstractInode
{
public:
	ChannelNode();
	ChannelNode(Channel* channel);
	virtual ~ChannelNode();
	void Construct(Channel* channel);
	virtual ssize_t read(ioctx_t* ctx, uint8_t* buf, size_t count);
	virtual ssize_t write(ioctx_t* ctx, const uint8_t* buf, size_t count);

private:
	Channel* channel;

};

class Server : public Refcountable
{
public:
	Server();
	virtual ~Server();
	void Disconnect();
	Channel* Connect();
	Channel* Listen();
	Ref<Inode> BootstrapNode(ino_t ino, mode_t type);
	Ref<Inode> OpenNode(ino_t ino, mode_t type);

private:
	kthread_mutex_t connect_lock;
	kthread_cond_t connecting_cond;
	kthread_cond_t connectable_cond;
	Channel* connecting;
	bool disconnected;

};

class ServerNode : public AbstractInode
{
public:
	ServerNode(Ref<Server> server);
	virtual ~ServerNode();
	virtual Ref<Inode> open(ioctx_t* ctx, const char* filename, int flags,
	                        mode_t mode);

private:
	Ref<Server> server;

};

class Unode : public Inode
{
public:
	Unode(Ref<Server> server, ino_t ino, mode_t type);
	virtual ~Unode();
	virtual void linked();
	virtual void unlinked();
	virtual int sync(ioctx_t* ctx);
	virtual int stat(ioctx_t* ctx, struct stat* st);
	virtual int statvfs(ioctx_t* ctx, struct statvfs* stvfs);
	virtual int chmod(ioctx_t* ctx, mode_t mode);
	virtual int chown(ioctx_t* ctx, uid_t owner, gid_t group);
	virtual int truncate(ioctx_t* ctx, off_t length);
	virtual off_t lseek(ioctx_t* ctx, off_t offset, int whence);
	virtual ssize_t read(ioctx_t* ctx, uint8_t* buf, size_t count);
	virtual ssize_t pread(ioctx_t* ctx, uint8_t* buf, size_t count,
	                      off_t off);
	virtual ssize_t write(ioctx_t* ctx, const uint8_t* buf, size_t count);
	virtual ssize_t pwrite(ioctx_t* ctx, const uint8_t* buf, size_t count,
	                       off_t off);
	virtual int utimens(ioctx_t* ctx, const struct timespec* atime,
	                    const struct timespec* ctime,
	                    const struct timespec* mtime);
	virtual int isatty(ioctx_t* ctx);
	virtual ssize_t readdirents(ioctx_t* ctx, struct kernel_dirent* dirent,
	                            size_t size, off_t start, size_t maxcount);
	virtual Ref<Inode> open(ioctx_t* ctx, const char* filename, int flags,
	                        mode_t mode);
	virtual int mkdir(ioctx_t* ctx, const char* filename, mode_t mode);
	virtual int link(ioctx_t* ctx, const char* filename, Ref<Inode> node);
	virtual int link_raw(ioctx_t* ctx, const char* filename, Ref<Inode> node);
	virtual int unlink(ioctx_t* ctx, const char* filename);
	virtual int unlink_raw(ioctx_t* ctx, const char* filename);
	virtual int rmdir(ioctx_t* ctx, const char* filename);
	virtual int rmdir_me(ioctx_t* ctx);
	virtual int symlink(ioctx_t* ctx, const char* oldname,
	                    const char* filename);
	virtual ssize_t readlink(ioctx_t* ctx, char* buf, size_t bufsiz);
	virtual int tcgetwincurpos(ioctx_t* ctx, struct wincurpos* wcp);
	virtual int tcgetwinsize(ioctx_t* ctx, struct winsize* ws);
	virtual int tcsetpgrp(ioctx_t* ctx, pid_t pgid);
	virtual pid_t tcgetpgrp(ioctx_t* ctx);
	virtual int settermmode(ioctx_t* ctx, unsigned mode);
	virtual int gettermmode(ioctx_t* ctx, unsigned* mode);
	virtual int poll(ioctx_t* ctx, PollNode* node);
	virtual int rename_here(ioctx_t* ctx, Ref<Inode> from, const char* oldname,
	                        const char* newname);
	virtual Ref<Inode> accept(ioctx_t* ctx, uint8_t* addr, size_t* addrlen,
	                          int flags);
	virtual int bind(ioctx_t* ctx, const uint8_t* addr, size_t addrlen);
	virtual int connect(ioctx_t* ctx, const uint8_t* addr, size_t addrlen);
	virtual int listen(ioctx_t* ctx, int backlog);
	virtual ssize_t recv(ioctx_t* ctx, uint8_t* buf, size_t count, int flags);
	virtual ssize_t send(ioctx_t* ctx, const uint8_t* buf, size_t count,
	                     int flags);

private:
	bool SendMessage(Channel* channel, size_t type, void* ptr, size_t size,
	                 size_t extra = 0);
	bool RecvMessage(Channel* channel, size_t type, void* ptr, size_t size);
	void RecvError(Channel* channel);
	bool RecvBoolean(Channel* channel);
	void UnexpectedResponse(Channel* channel, struct fsm_msg_header* hdr);

private:
	ioctx_t kctx;
	Ref<Server> server;

};

//
// Implementation of Channel Directory.
//

ChannelDirection::ChannelDirection()
{
	buffer_used = 0;
	buffer_offset = 0;
	transfer_lock = KTHREAD_MUTEX_INITIALIZER;
	not_empty = KTHREAD_COND_INITIALIZER;
	not_full = KTHREAD_COND_INITIALIZER;
	still_reading = true;
	still_writing = true;
}

ChannelDirection::~ChannelDirection()
{
}

size_t ChannelDirection::Send(ioctx_t* ctx, const void* ptr, size_t least, size_t max)
{
	const uint8_t* src = (const uint8_t*) ptr;
	size_t sofar = 0;
	ScopedLock inner_lock(&transfer_lock);
	while ( true )
	{
		while ( true )
		{
			if ( !still_reading )
				return errno = ECONNRESET, sofar;
			if ( buffer_used < BUFFER_SIZE )
				break;
			if ( least <= sofar )
				return sofar;
			if ( !kthread_cond_wait_signal(&not_full, &transfer_lock) )
				return errno = EINTR, sofar;
		}

		size_t use_offset = (buffer_offset + buffer_used) % BUFFER_SIZE;
		size_t count = max - sofar;
		size_t available_to_end = BUFFER_SIZE - use_offset;
		size_t available = BUFFER_SIZE - buffer_used;
		if ( available_to_end < available )
			available = available_to_end;
		if ( available < count )
			count = available;
		if ( !ctx->copy_from_src(buffer + use_offset, src + sofar, count) )
			return sofar;
		if ( !buffer_used )
			kthread_cond_signal(&not_empty);
		buffer_used += count;
		sofar += count;
		if ( sofar == max )
			return sofar;
	}
}

size_t ChannelDirection::Recv(ioctx_t* ctx, void* ptr, size_t least, size_t max)
{
	uint8_t* dst = (uint8_t*) ptr;
	size_t sofar = 0;
	ScopedLock inner_lock(&transfer_lock);
	while ( true )
	{
		while ( true )
		{
			if ( buffer_used )
				break;
			if ( least <= sofar )
				return sofar;
			if ( !still_writing )
				return errno = ECONNRESET, sofar;
			if ( !kthread_cond_wait_signal(&not_empty, &transfer_lock) )
				return errno = EINTR, sofar;
		}

		size_t use_offset = buffer_offset;
		size_t count = max - sofar;
		size_t available_to_end = BUFFER_SIZE - use_offset;
		size_t available = buffer_used;
		if ( available_to_end < available )
			available = available_to_end;
		if ( available < count )
			count = available;
		if ( !ctx->copy_to_dest(dst + sofar, buffer + use_offset, count) )
			return sofar;
		if ( buffer_used == BUFFER_SIZE )
			kthread_cond_signal(&not_full);
		buffer_offset = (buffer_offset + count) % BUFFER_SIZE;
		buffer_used -= count;
		sofar += count;
		if ( sofar == max )
			return sofar;
	}
}

void ChannelDirection::SendClose()
{
	ScopedLock lock(&transfer_lock);
	still_writing = false;
	kthread_cond_signal(&not_empty);
}

void ChannelDirection::RecvClose()
{
	ScopedLock lock(&transfer_lock);
	still_writing = false;
	kthread_cond_signal(&not_full);
}

//
// Implementation of Channel.
//

Channel::Channel()
{
	kernel_lock = KTHREAD_MUTEX_INITIALIZER;
	user_lock = KTHREAD_MUTEX_INITIALIZER;
	destruction_lock = KTHREAD_MUTEX_INITIALIZER;
	user_closed = false;
	kernel_closed = false;
}

Channel::~Channel()
{
}

size_t Channel::KernelSend(ioctx_t* ctx, const void* ptr, size_t least,
                            size_t max)
{
	ScopedLockSignal outer_lock(&kernel_lock);
	if ( !outer_lock.IsAcquired() )
		return errno = EINTR, 0;
	size_t ret = from_kernel.Send(ctx, ptr, least, max);
	return ret;
}

size_t Channel::KernelRecv(ioctx_t* ctx, void* ptr, size_t least, size_t max)
{
	ScopedLockSignal outer_lock(&kernel_lock);
	if ( !outer_lock.IsAcquired() )
		return errno = EINTR, 0;
	return from_user.Recv(ctx, ptr, least, max);
}

void Channel::KernelClose()
{
	// No lock needed, this thread is the last to use this object as kernel.
	from_kernel.SendClose();
	from_user.RecvClose();
	kthread_mutex_lock(&destruction_lock);
	kernel_closed = true;
	bool delete_this = user_closed;
	kthread_mutex_unlock(&destruction_lock);
	if ( delete_this )
		delete this;
}

size_t Channel::UserSend(ioctx_t* ctx, const void* ptr, size_t least,
                         size_t max)
{
	ScopedLockSignal outer_lock(&user_lock);
	if ( !outer_lock.IsAcquired() )
		return errno = EINTR, 0;
	return from_user.Send(ctx, ptr, least, max);
}

size_t Channel::UserRecv(ioctx_t* ctx, void* ptr, size_t least, size_t max)
{
	ScopedLockSignal outer_lock(&user_lock);
	if ( !outer_lock.IsAcquired() )
		return errno = EINTR, 0;
	return from_kernel.Recv(ctx, ptr, least, max);
}

void Channel::UserClose()
{
	// No lock needed, this thread is the last to use this object as user.
	from_kernel.RecvClose();
	from_user.SendClose();
	kthread_mutex_lock(&destruction_lock);
	user_closed = true;
	bool delete_this = kernel_closed;
	kthread_mutex_unlock(&destruction_lock);
	if ( delete_this )
		delete this;
}

//
// Implementation of ChannelNode.
//

ChannelNode::ChannelNode()
{
	channel = NULL;
}

ChannelNode::ChannelNode(Channel* channel)
{
	Construct(channel);
}

ChannelNode::~ChannelNode()
{
	if ( channel )
		channel->UserClose();
}

void ChannelNode::Construct(Channel* channel)
{
	inode_type = INODE_TYPE_STREAM;
	this->channel = channel;
	this->type = S_IFCHR;
	this->dev = (dev_t) this;
	this->ino = 0;
	// TODO: Set uid, gid, mode.
}

ssize_t ChannelNode::read(ioctx_t* ctx, uint8_t* buf, size_t count)
{
	return channel->UserRecv(ctx, buf, /*1*/ count, count);
}

ssize_t ChannelNode::write(ioctx_t* ctx, const uint8_t* buf, size_t count)
{
	return channel->UserSend(ctx, buf, /*1*/ count, count);
}

//
// Implementation of Server.
//

Server::Server()
{
	connect_lock = KTHREAD_MUTEX_INITIALIZER;
	connecting_cond = KTHREAD_COND_INITIALIZER;
	connectable_cond = KTHREAD_COND_INITIALIZER;
	connecting = NULL;
	disconnected = false;
}

Server::~Server()
{
}

void Server::Disconnect()
{
	ScopedLock lock(&connect_lock);
	disconnected = true;
	kthread_cond_signal(&connectable_cond);
}

Channel* Server::Connect()
{
	Channel* channel = new Channel();
	if ( !channel )
		return NULL;
	ScopedLock lock(&connect_lock);
	while ( !disconnected && connecting )
		if ( !kthread_cond_wait_signal(&connectable_cond, &connect_lock) )
		{
			delete channel;
			return errno = EINTR, (Channel*) NULL;
		}
	if ( disconnected )
		return delete channel, errno = ECONNREFUSED, (Channel*) NULL;
	connecting = channel;
	kthread_cond_signal(&connecting_cond);
	return channel;
}

Channel* Server::Listen()
{
	ScopedLock lock(&connect_lock);
	while ( !connecting )
		if ( !kthread_cond_wait_signal(&connecting_cond, &connect_lock) )
			return errno = EINTR, (Channel*) NULL;
	Channel* result = connecting;
	connecting = NULL;
	kthread_cond_signal(&connectable_cond);
	return result;
}

Ref<Inode> Server::BootstrapNode(ino_t ino, mode_t type)
{
	return Ref<Inode>(new Unode(Ref<Server>(this), ino, type));
}

Ref<Inode> Server::OpenNode(ino_t ino, mode_t type)
{
	return BootstrapNode(ino, type);
}

//
// Implementation of ServerNode.
//

ServerNode::ServerNode(Ref<Server> server)
{
	inode_type = INODE_TYPE_UNKNOWN;
	this->server = server;
	this->type = S_IFDIR;
	this->dev = (dev_t) this;
	this->ino = 0;
	// TODO: Set uid, gid, mode.
}

ServerNode::~ServerNode()
{
	server->Disconnect();
}

Ref<Inode> ServerNode::open(ioctx_t* /*ctx*/, const char* filename, int flags,
                            mode_t mode)
{
	unsigned long long ull_ino;
	char* end;
	int saved_errno = errno; errno = 0;
	if ( !isspace(*filename) &&
	     0 < (ull_ino = strtoull(filename, &end, 10)) &&
	     *end == '\0' &&
	     errno != ERANGE )
	{
		errno = saved_errno;
		if ( !(flags & O_CREATE) )
			return errno = ENOENT, Ref<Inode>(NULL);
		ino_t ino = (ino_t) ull_ino;
		return server->BootstrapNode(ino, mode & S_IFMT);
	}
	errno = saved_errno;
	if ( !strcmp(filename, "listen") )
	{
		Ref<ChannelNode> node(new ChannelNode);
		if ( !node )
			return Ref<Inode>(NULL);
		Channel* channel = server->Listen();
		if ( !channel )
			return Ref<Inode>(NULL);
		node->Construct(channel);
		return node;
	}
	return errno = ENOENT, Ref<Inode>(NULL);
}

//
// Implementation of Unode.
//

Unode::Unode(Ref<Server> server, ino_t ino, mode_t type)
{
	SetupKernelIOCtx(&kctx);
	this->server = server;
	this->ino = ino;
	this->dev = (dev_t) server;
	this->type = type;

	// Let the remote know that the kernel is using this inode.
	if ( Channel* channel = server->Connect() )
	{
		struct fsm_req_refer msg;
		msg.ino = ino;
		SendMessage(channel, FSM_REQ_REFER, &msg, sizeof(msg));
		channel->KernelClose();
	}
}

Unode::~Unode()
{
	// Let the remote know that the kernel is no longer using this inode.
	if ( Channel* channel = server->Connect() )
	{
		struct fsm_req_unref msg;
		msg.ino = ino;
		SendMessage(channel, FSM_REQ_UNREF, &msg, sizeof(msg));
		channel->KernelClose();
	}
}

bool Unode::SendMessage(Channel* channel, size_t type, void* ptr, size_t size,
                        size_t extra)
{
	struct fsm_msg_header hdr;
	hdr.msgtype = type;
	hdr.msgsize = size + extra;
	if ( !channel->KernelSend(&kctx, &hdr, sizeof(hdr)) )
		return false;
	if ( !channel->KernelSend(&kctx, ptr, size) )
		return false;
	return true;
}

bool Unode::RecvMessage(Channel* channel, size_t type, void* ptr, size_t size)
{
	struct fsm_msg_header resp_hdr;
	if ( !channel->KernelRecv(&kctx, &resp_hdr, sizeof(resp_hdr)) )
		return false;
	if ( resp_hdr.msgtype != type )
	{
		UnexpectedResponse(channel, &resp_hdr);
		return false;
	}
	return  !ptr || !size || channel->KernelRecv(&kctx, ptr, size);
}

void Unode::RecvError(Channel* channel)
{
	SetupKernelIOCtx(&kctx);
	struct fsm_resp_error resp;
	if ( channel->KernelRecv(&kctx, &resp, sizeof(resp)) )
		errno = resp.errnum;
	// In case of error, errno is set to that error.
}

bool Unode::RecvBoolean(Channel* channel)
{
	struct fsm_msg_header resp_hdr;
	if ( !channel->KernelRecv(&kctx, &resp_hdr, sizeof(resp_hdr)) )
		return false;
	if ( resp_hdr.msgtype == FSM_RESP_SUCCESS )
		return true;
	UnexpectedResponse(channel, &resp_hdr);
	return false;
}

void Unode::UnexpectedResponse(Channel* channel, struct fsm_msg_header* hdr)
{
	if ( hdr->msgtype == FSM_RESP_ERROR )
		RecvError(channel);
	else
		errno = EIO;
}

void Unode::linked()
{
}

void Unode::unlinked()
{
}

int Unode::sync(ioctx_t* /*ctx*/)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_sync msg;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_SYNC, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::stat(ioctx_t* ctx, struct stat* st)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_stat msg;
	struct fsm_resp_stat resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_STAT, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_STAT, &resp, sizeof(resp)) &&
	     (resp.st.st_dev = (dev_t) server, true) &&
	     ctx->copy_to_dest(st, &resp.st, sizeof(*st)) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::statvfs(ioctx_t* ctx, struct statvfs* stvfs)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_statvfs msg;
	struct fsm_resp_statvfs resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_STATVFS, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_STATVFS, &resp, sizeof(resp)) &&
	     (resp.stvfs.f_fsid = (dev_t) server, true) &&
	     (resp.stvfs.f_flag |= ST_NOSUID, true) &&
	     ctx->copy_to_dest(stvfs, &resp.stvfs, sizeof(*stvfs)) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::chmod(ioctx_t* /*ctx*/, mode_t mode)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_chmod msg;
	msg.ino = ino;
	msg.mode = mode;
	if ( SendMessage(channel, FSM_REQ_CHMOD, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::chown(ioctx_t* /*ctx*/, uid_t owner, gid_t group)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_chown msg;
	msg.ino = ino;
	msg.uid = owner;
	msg.gid = group;
	if ( SendMessage(channel, FSM_REQ_CHOWN, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::truncate(ioctx_t* /*ctx*/, off_t length)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_truncate msg;
	msg.ino = ino;
	msg.size = length;
	if ( SendMessage(channel, FSM_REQ_TRUNCATE, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

off_t Unode::lseek(ioctx_t* /*ctx*/, off_t offset, int whence)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	off_t ret = -1;
	struct fsm_req_lseek msg;
	struct fsm_resp_lseek resp;
	msg.ino = ino;
	msg.offset = offset;
	msg.whence = whence;
	if ( SendMessage(channel, FSM_REQ_LSEEK, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_LSEEK, &resp, sizeof(resp)) )
		ret = resp.offset;
	channel->KernelClose();
	return ret;
}

ssize_t Unode::read(ioctx_t* ctx, uint8_t* buf, size_t count)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	ssize_t ret = -1;
	struct fsm_req_read msg;
	struct fsm_resp_read resp;
	msg.ino = ino;
	msg.count = count;
	if ( SendMessage(channel, FSM_REQ_READ, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_READ, &resp, sizeof(resp)) )
	{
		if ( resp.count < count )
			count = resp.count;
		if ( channel->KernelRecv(ctx, buf, count) )
			ret = (ssize_t) count;
	}
	channel->KernelClose();
	return ret;
}

ssize_t Unode::pread(ioctx_t* ctx, uint8_t* buf, size_t count, off_t off)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	ssize_t ret = -1;
	struct fsm_req_pread msg;
	struct fsm_resp_read resp;
	msg.ino = ino;
	msg.count = count;
	msg.offset = off;
	if ( SendMessage(channel, FSM_REQ_PREAD, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_READ, &resp, sizeof(resp)) )
	{
		if ( resp.count < count )
			count = resp.count;
		if ( channel->KernelRecv(ctx, buf, count) )
			ret = (ssize_t) count;
	}
	channel->KernelClose();
	return ret;
}

ssize_t Unode::write(ioctx_t* ctx, const uint8_t* buf, size_t count)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_write msg;
	msg.ino = ino;
	msg.count = count;
	struct fsm_msg_header hdr;
	hdr.msgtype = FSM_REQ_WRITE;
	hdr.msgsize = sizeof(msg) + count;
	struct fsm_resp_write resp;
	if ( channel->KernelSend(&kctx, &hdr, sizeof(hdr)) &&
	     channel->KernelSend(&kctx, &msg, sizeof(msg)) &&
	     channel->KernelSend(ctx, buf, count) &&
	     RecvMessage(channel, FSM_RESP_WRITE, &resp, sizeof(resp)) )
		ret = (ssize_t) resp.count;
	channel->KernelClose();
	return ret;
}

ssize_t Unode::pwrite(ioctx_t* ctx, const uint8_t* buf, size_t count, off_t off)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	ssize_t ret = -1;
	struct fsm_req_pwrite msg;
	msg.ino = ino;
	msg.count = count;
	msg.offset = off;
	struct fsm_msg_header hdr;
	hdr.msgtype = FSM_REQ_PWRITE;
	hdr.msgsize = sizeof(msg) + count;
	struct fsm_resp_write resp;
	if ( channel->KernelSend(&kctx, &hdr, sizeof(hdr)) &&
	     channel->KernelSend(&kctx, &msg, sizeof(msg)) &&
	     channel->KernelSend(ctx, buf, count) &&
	     RecvMessage(channel, FSM_RESP_WRITE, &resp, sizeof(resp)) )
		ret = (ssize_t) resp.count;
	channel->KernelClose();
	return ret;
}

int Unode::utimens(ioctx_t* /*ctx*/,
                   const struct timespec* atime,
                   const struct timespec* /*ctime*/,
                   const struct timespec* mtime)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_utimens msg;
	msg.ino = ino;
	msg.times[0] = atime ? *atime : timespec_nul();
	msg.times[1] = mtime ? *mtime : timespec_nul();
	if ( SendMessage(channel, FSM_REQ_UTIMENS, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::isatty(ioctx_t* /*ctx*/)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return 0;
	int ret = 0;
	struct fsm_req_isatty msg;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_ISATTY, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 1;
	channel->KernelClose();
	return ret;
}

ssize_t Unode::readdirents(ioctx_t* ctx, struct kernel_dirent* dirent,
                           size_t size, off_t start, size_t /*maxcount*/)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	ssize_t ret = -1;
	struct fsm_req_readdirents msg;
	struct fsm_resp_readdirents resp;
	msg.ino = ino;
	msg.rec_num = start;
	errno = 0;
	if ( SendMessage(channel, FSM_REQ_READDIRENTS, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_READDIRENTS, &resp, sizeof(resp)) )
	{
		if ( !resp.namelen )
		{
			ret = 0;
			goto break_if;
		}

		struct kernel_dirent entry;
		entry.d_reclen = sizeof(entry) + resp.namelen + 1;
		entry.d_nextoff = 0;
		entry.d_namlen = resp.namelen;
		entry.d_dev = (dev_t) server;
		entry.d_ino = resp.ino;
		entry.d_type = resp.type;

		if ( !ctx->copy_to_dest(dirent, &entry, sizeof(entry)) )
			goto break_if;

		size_t needed = sizeof(*dirent) + resp.namelen + 1;
		if ( size < needed && (errno = ERANGE) )
			goto break_if;

		uint8_t nul = 0;
		if ( channel->KernelRecv(ctx, dirent->d_name, resp.namelen) &&
		     ctx->copy_to_dest(&dirent->d_name[resp.namelen], &nul, 1) )
			ret = (ssize_t) needed;
	} break_if:
	channel->KernelClose();
	return ret;
}

Ref<Inode> Unode::open(ioctx_t* /*ctx*/, const char* filename, int flags,
                       mode_t mode)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return Ref<Inode>(NULL);
	size_t filenamelen = strlen(filename);
	Ref<Inode> ret;
	struct fsm_req_open msg;
	msg.dirino = ino;
	msg.flags = flags;
	msg.mode = mode;
	msg.namelen = filenamelen;
	struct fsm_resp_open resp;
	if ( SendMessage(channel, FSM_REQ_OPEN, &msg, sizeof(msg), filenamelen) &&
	     channel->KernelSend(&kctx, filename, filenamelen) &&
	     RecvMessage(channel, FSM_RESP_OPEN, &resp, sizeof(resp)) )
		ret = server->OpenNode(resp.ino, resp.type);
	channel->KernelClose();
	return ret;
}

int Unode::mkdir(ioctx_t* /*ctx*/, const char* filename, mode_t mode)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return 0;
	size_t filenamelen = strlen(filename);
	int ret = -1;
	struct fsm_req_mkdir msg;
	msg.dirino = ino;
	msg.mode = mode;
	msg.namelen = filenamelen;
	struct fsm_resp_mkdir resp;
	if ( SendMessage(channel, FSM_REQ_MKDIR, &msg, sizeof(msg), filenamelen) &&
	     channel->KernelSend(&kctx, filename, filenamelen) &&
	     RecvMessage(channel, FSM_RESP_MKDIR, &resp, sizeof(resp)) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::link(ioctx_t* /*ctx*/, const char* filename, Ref<Inode> node)
{
	if ( node->dev != this->dev )
		return errno = EXDEV, -1;
	Channel* channel = server->Connect();
	if ( !channel )
		return 0;
	size_t filenamelen = strlen(filename);
	int ret = -1;
	struct fsm_req_link msg;
	msg.dirino = ino;
	msg.linkino = node->ino;
	msg.namelen = filenamelen;
	if ( SendMessage(channel, FSM_REQ_LINK, &msg, sizeof(msg), filenamelen) &&
	     channel->KernelSend(&kctx, filename, filenamelen) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::link_raw(ioctx_t* /*ctx*/, const char* /*filename*/, Ref<Inode> /*node*/)
{
	return errno = EPERM, -1;
}

int Unode::unlink(ioctx_t* /*ctx*/, const char* filename)
{
	// TODO: Make sure the target is no longer used!
	Channel* channel = server->Connect();
	if ( !channel )
		return 0;
	size_t filenamelen = strlen(filename);
	int ret = -1;
	struct fsm_req_unlink msg;
	msg.dirino = ino;
	msg.namelen = filenamelen;
	if ( SendMessage(channel, FSM_REQ_UNLINK, &msg, sizeof(msg), filenamelen) &&
	     channel->KernelSend(&kctx, filename, filenamelen) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::unlink_raw(ioctx_t* /*ctx*/, const char* /*filename*/)
{
	return errno = EPERM, -1;
}

int Unode::rmdir(ioctx_t* /*ctx*/, const char* filename)
{
	// TODO: Make sure the target is no longer used!
	Channel* channel = server->Connect();
	if ( !channel )
		return 0;
	size_t filenamelen = strlen(filename);
	int ret = -1;
	struct fsm_req_rmdir msg;
	msg.dirino = ino;
	msg.namelen = filenamelen;
	if ( SendMessage(channel, FSM_REQ_RMDIR, &msg, sizeof(msg), filenamelen) &&
	     channel->KernelSend(&kctx, filename, filenamelen) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::rmdir_me(ioctx_t* /*ctx*/)
{
	return errno = EPERM, -1;
}

int Unode::symlink(ioctx_t* /*ctx*/, const char* oldname, const char* filename)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return 0;
	size_t oldnamelen = strlen(oldname);
	size_t filenamelen = strlen(filename);
	int ret = -1;
	struct fsm_req_symlink msg;
	msg.dirino = ino;
	msg.targetlen = oldnamelen;
	msg.namelen = filenamelen;
	size_t extra = msg.targetlen + msg.namelen;
	if ( SendMessage(channel, FSM_REQ_SYMLINK, &msg, sizeof(msg), extra) &&
	     channel->KernelSend(&kctx, oldname, oldnamelen) &&
	     channel->KernelSend(&kctx, filename, filenamelen) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

ssize_t Unode::readlink(ioctx_t* ctx, char* buf, size_t bufsiz)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	ssize_t ret = -1;
	struct fsm_req_readlink msg;
	struct fsm_resp_readlink resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_READLINK, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_READLINK, &resp, sizeof(resp)) )
	{
		if ( resp.targetlen < bufsiz )
			bufsiz = resp.targetlen;
		if ( channel->KernelRecv(ctx, buf, bufsiz) )
			ret = (ssize_t) bufsiz;
	}
	channel->KernelClose();
	return ret;
}

int Unode::tcgetwincurpos(ioctx_t* ctx, struct wincurpos* wcp)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_tcgetwincurpos msg;
	struct fsm_resp_tcgetwincurpos resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_TCGETWINCURPOS, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_TCGETWINCURPOS, &resp, sizeof(resp)) &&
	     ctx->copy_to_dest(wcp, &resp.pos, sizeof(*wcp)) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::tcgetwinsize(ioctx_t* ctx, struct winsize* ws)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_tcgetwinsize msg;
	struct fsm_resp_tcgetwinsize resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_TCGETWINSIZE, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_TCGETWINSIZE, &resp, sizeof(resp)) &&
	     ctx->copy_to_dest(ws, &resp.size, sizeof(*ws)) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::tcsetpgrp(ioctx_t* /*ctx*/, pid_t pgid)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_tcsetpgrp msg;
	msg.ino = ino;
	msg.pgid = pgid;
	if ( SendMessage(channel, FSM_REQ_TCSETPGRP, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

pid_t Unode::tcgetpgrp(ioctx_t* /*ctx*/)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	pid_t ret = -1;
	struct fsm_req_tcgetpgrp msg;
	struct fsm_resp_tcgetpgrp resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_TCGETPGRP, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_TCGETPGRP, &resp, sizeof(resp)) )
		ret = resp.pgid;
	channel->KernelClose();
	return ret;
}

int Unode::settermmode(ioctx_t* /*ctx*/, unsigned mode)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_settermmode msg;
	msg.ino = ino;
	msg.termmode = mode;
	if ( SendMessage(channel, FSM_REQ_SETTERMMODE, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::gettermmode(ioctx_t* ctx, unsigned* mode)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_gettermmode msg;
	struct fsm_resp_gettermmode resp;
	msg.ino = ino;
	if ( SendMessage(channel, FSM_REQ_GETTERMMODE, &msg, sizeof(msg)) &&
	     RecvMessage(channel, FSM_RESP_GETTERMMODE, &resp, sizeof(resp)) &&
	     ctx->copy_to_dest(mode, &resp.termmode, sizeof(*mode)) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

int Unode::poll(ioctx_t* /*ctx*/, PollNode* /*node*/)
{
	return errno = ENOTSUP, -1;
}

int Unode::rename_here(ioctx_t* /*ctx*/, Ref<Inode> from, const char* oldname,
                       const char* newname)
{
	Channel* channel = server->Connect();
	if ( !channel )
		return -1;
	int ret = -1;
	struct fsm_req_rename msg;
	msg.olddirino = this->ino;
	msg.newdirino = from->ino;
	msg.oldnamelen = strlen(oldname);
	msg.newnamelen = strlen(newname);
	size_t extra = msg.oldnamelen + msg.newnamelen;
	if ( SendMessage(channel, FSM_REQ_RENAME, &msg, sizeof(msg), extra) &&
	     channel->KernelSend(&kctx, oldname, msg.oldnamelen) &&
	     channel->KernelSend(&kctx, newname, msg.newnamelen) &&
	     RecvMessage(channel, FSM_RESP_SUCCESS, NULL, 0) )
		ret = 0;
	channel->KernelClose();
	return ret;
}

Ref<Inode> Unode::accept(ioctx_t* /*ctx*/, uint8_t* /*addr*/,
                         size_t* /*addrlen*/, int /*flags*/)
{
	return errno = ENOTSOCK, Ref<Inode>();
}

int Unode::bind(ioctx_t* /*ctx*/, const uint8_t* /*addr*/, size_t /*addrlen*/)
{
	return errno = ENOTSOCK, -1;
}

int Unode::connect(ioctx_t* /*ctx*/, const uint8_t* /*addr*/, size_t /*addrlen*/)
{
	return errno = ENOTSOCK, -1;
}

int Unode::listen(ioctx_t* /*ctx*/, int /*backlog*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t Unode::recv(ioctx_t* /*ctx*/, uint8_t* /*buf*/, size_t /*count*/,
                    int /*flags*/)
{
	return errno = ENOTSOCK, -1;
}

ssize_t Unode::send(ioctx_t* /*ctx*/, const uint8_t* /*buf*/, size_t /*count*/,
                    int /*flags*/)
{
	return errno = ENOTSOCK, -1;
}

//
// Initialization.
//

class FactoryNode : public AbstractInode
{
public:
	FactoryNode(uid_t owner, gid_t group, mode_t mode);
	virtual ~FactoryNode() { }
	virtual Ref<Inode> open(ioctx_t* ctx, const char* filename, int flags,
	                        mode_t mode);

};

FactoryNode::FactoryNode(uid_t owner, gid_t group, mode_t mode)
{
	inode_type = INODE_TYPE_UNKNOWN;
	dev = (dev_t) this;
	ino = 0;
	this->type = S_IFDIR;
	this->stat_uid = owner;
	this->stat_gid = group;
	this->stat_mode = (mode & S_SETABLE) | this->type;
}

Ref<Inode> FactoryNode::open(ioctx_t* /*ctx*/, const char* filename,
                             int /*flags*/, mode_t /*mode*/)
{
	if ( !strcmp(filename, "new") )
	{
		Ref<Server> server(new Server());
		if ( !server )
			return Ref<Inode>(NULL);
		Ref<ServerNode> node(new ServerNode(server));
		if ( !node )
			return Ref<Inode>(NULL);
		return node;
	}
	return errno = ENOENT, Ref<Inode>(NULL);
}

static int sys_fsm_fsbind(int rootfd, int mpointfd, int /*flags*/)
{
	Ref<Descriptor> desc = CurrentProcess()->GetDescriptor(rootfd);
	if ( !desc ) { return -1; }
	Ref<Descriptor> mpoint = CurrentProcess()->GetDescriptor(mpointfd);
	if ( !mpoint ) { return -1; }
	Ref<MountTable> mtable = CurrentProcess()->GetMTable();
	Ref<Inode> node = desc->vnode->inode;
	return mtable->AddMount(mpoint->ino, mpoint->dev, node) ? 0 : -1;
}

void Init(const char* devpath, Ref<Descriptor> slashdev)
{
	ioctx_t ctx; SetupKernelIOCtx(&ctx);
	Ref<Inode> node(new FactoryNode(0, 0, 0666));
	if ( !node )
		PanicF("Unable to allocate %s/fs inode.", devpath);
	// TODO: Race condition! Create a mkdir function that returns what it
	// created, possibly with a O_MKDIR flag to open.
	if ( slashdev->mkdir(&ctx, "fs", 0755) < 0 && errno != EEXIST )
		PanicF("Could not create a %s/fs directory", devpath);
	Ref<Descriptor> mpoint = slashdev->open(&ctx, "fs", O_READ | O_WRITE, 0);
	if ( !mpoint )
		PanicF("Could not open the %s/fs directory", devpath);
	Ref<MountTable> mtable = CurrentProcess()->GetMTable();
	// TODO: Make sure that the mount point is *empty*! Add a proper function
	// for this on the file descriptor class!
	if ( !mtable->AddMount(mpoint->ino, mpoint->dev, node) )
		PanicF("Unable to mount filesystem on %s/fs", devpath);

	Syscall::Register(SYSCALL_FSM_FSBIND, (void*) sys_fsm_fsbind);
}

} // namespace UserFS
} // namespace Sortix
