/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU Go, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007,   *
 * 2008 and 2009 by the Free Software Foundation.                    *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 3 or          *
 * (at your option) any later version.                               *
 *                                                                   *
 * This program is distributed in the hope that it will be useful,   *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 * GNU General Public License in file COPYING for more details.      *
 *                                                                   *
 * You should have received a copy of the GNU General Public         *
 * License along with this program; if not, write to the Free        *
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,       *
 * Boston, MA 02111, USA.                                            *
 \* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Convert joseki from sgf format to patterns.db format. */

#include "mboard.h"
#include "mgnugo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define USAGE "\
Usage : joseki prefix filename\n\
"

/* Joseki move types. */
#define STANDARD  0
#define URGENT    1
#define MINOR     2
#define TRICK     3
#define ANTISUJI  4
#define TENUKI_OK 5

/* This is used for both the dragon status and safety fields.
 * Also used for unconditional status in struct worm_data and for the
 * final status computed by the aftermath code.
 */
enum dragon_status {
	DEAD,
	ALIVE,
	CRITICAL,
	UNKNOWN,
	UNCHECKED,
	CAN_THREATEN_ATTACK,
	CAN_THREATEN_DEFENSE,
	INESSENTIAL,
	TACTICALLY_DEAD,
	ALIVE_IN_SEKI,
	STRONGLY_ALIVE,
	INVINCIBLE,
	INSUBSTANTIAL,
	WHITE_TERRITORY,
	BLACK_TERRITORY,
	DAME,
	NUM_DRAGON_STATUS
};

/* We don't want to play moves on edges of board which might have been
 * cropped, since there might appear an accidential capture.
 */
#define SAFE_ON_BOARD(i, j) ((i) >= 0 && (j) >= 0\
			     && (i) < MAX_BOARD - 1 && (j) < MAX_BOARD - 1)

static int boardsize;

static int showdead = 0;

/* Unreasonable score used to detect missing information. */
#define NO_SCORE 4711
/* Keep track of the score estimated before the last computer move. */
static int current_score_estimate = NO_SCORE;

/* This array contains +'s and -'s for the empty board positions.
 * hspot_size contains the board size that the grid has been
 * initialized to.
 */

static int hspot_size;
static char hspots[MAX_BOARD][MAX_BOARD];

static int clock_on = 0;

/*
 * data concerning a dragon. A copy is kept at each stone of the string.
 */

struct dragon_data {
	int color; /* its color                                                 */
	int id; /* the index into the dragon2 array                          */
	int origin; /* the origin of the dragon. Two vertices are in the same    */
	/* dragon iff they have same origin.                         */
	int size; /* size of the dragon                                        */
	float effective_size; /* stones and surrounding spaces                     */
	enum dragon_status crude_status; /* (ALIVE, DEAD, UNKNOWN, CRITICAL)       */
	enum dragon_status status; /* best trusted status                    */
};

struct dragon_data dragon[BOARDMAX];

enum dragon_status dragon_status(int pos) {
	return dragon[pos].status;
}

/* Identify the type of joseki move.
 * FIXME: We might want the relax the requirement that this info comes
 *        as the very first character.
 */
static int identify_move_type(char *text) {
	if (!text)
		return STANDARD;

	switch ((int) *text) {
	case 'u':
	case 'U':
		return URGENT;
		break;
	case 'J':
	case 'S':
		return STANDARD;
		break;
	case 'j':
	case 's':
		return MINOR;
		break;
	case 'T':
		return TRICK;
		break;
	case 't':
		return TENUKI_OK;
		break;
	case '0':
	case 'a':
	case 'A':
		return ANTISUJI;
		break;
	}

	return STANDARD;
}

/*
 * Create letterbar for the top and bottom of the ASCII board.
 */

static void make_letterbar(int boardsize, char *letterbar) {
	int i, letteroffset;
	char spaces[64];
	char letter[64];

	if (boardsize <= 25)
		strcpy(spaces, " ");
	strcpy(letterbar, "   ");

	for (i = 0; i < boardsize; i++) {
		letteroffset = 'A';
		if (i + letteroffset >= 'I')
			letteroffset++;
		strcat(letterbar, spaces);
		sprintf(letter, "%c", i + letteroffset);
		strcat(letterbar, letter);
	}
}

/*
 * Mark the handicap spots on the board.
 */

static void set_handicap_spots(int boardsize) {
	if (hspot_size == boardsize)
		return;

	hspot_size = boardsize;

	memset(hspots, '.', sizeof(hspots));

	if (boardsize == 5) {
		/* place the outer 4 */
		hspots[1][1] = '+';
		hspots[boardsize - 2][1] = '+';
		hspots[1][boardsize - 2] = '+';
		hspots[boardsize - 2][boardsize - 2] = '+';
		/* and the middle one */
		hspots[boardsize / 2][boardsize / 2] = '+';
		return;
	}

	if (!(boardsize % 2)) {
		/* If the board size is even, no center handicap spots. */
		if (boardsize > 2 && boardsize < 12) {
			/* Place the outer 4 only. */
			hspots[2][2] = '+';
			hspots[boardsize - 3][2] = '+';
			hspots[2][boardsize - 3] = '+';
			hspots[boardsize - 3][boardsize - 3] = '+';
		} else {
			/* Place the outer 4 only. */
			hspots[3][3] = '+';
			hspots[boardsize - 4][3] = '+';
			hspots[3][boardsize - 4] = '+';
			hspots[boardsize - 4][boardsize - 4] = '+';
		}
	} else {
		/* Uneven board size */
		if (boardsize > 2 && boardsize < 12) {
			/* Place the outer 4... */
			hspots[2][2] = '+';
			hspots[boardsize - 3][2] = '+';
			hspots[2][boardsize - 3] = '+';
			hspots[boardsize - 3][boardsize - 3] = '+';

			/* ...and the middle one. */
			hspots[boardsize / 2][boardsize / 2] = '+';
		} else if (boardsize > 12) {
			/* Place the outer 4... */
			hspots[3][3] = '+';
			hspots[boardsize - 4][3] = '+';
			hspots[3][boardsize - 4] = '+';
			hspots[boardsize - 4][boardsize - 4] = '+';

			/* ...and the inner 4... */
			hspots[3][boardsize / 2] = '+';
			hspots[boardsize / 2][3] = '+';
			hspots[boardsize / 2][boardsize - 4] = '+';
			hspots[boardsize - 4][boardsize / 2] = '+';

			/* ...and the middle one. */
			hspots[boardsize / 2][boardsize / 2] = '+';
		}
	}

	return;
}

/* Copy the lines starting with a certain character to stdout. */
static void write_selected_lines(char *text, char start_char) {
	char *p;
	if (!text)
		return;
	while (1) {
		p = strchr(text, '\n');
		if (p)
			*p = 0;
		if (*text == start_char)
			printf("%s\n", text);
		if (p) {
			*p = '\n';
			text = p + 1;
		} else
			break;
	}
}

/* Is there any line starting with a certain character? */
static int selected_line_exists(char *text, char start_char) {
	char *p;
	if (!text)
		return 0;
	while (1) {
		if (*text == start_char)
			return 1;
		p = strchr(text, '\n');
		if (p)
			text = p + 1;
		else
			break;
	}
	return 0;
}

/* Write the main diagram or the constraint diagram. In the former
 * case, pass a NULL pointer for labels.
 */
static void write_diagram(int movei, int movej, int color, int marki, int markj,
		char labels[MAX_BOARD][MAX_BOARD]) {
	int i, j;

	for (i = -1; i <= marki; i++) {
		for (j = markj; j >= 0; j--) {
			if (i == -1)
				printf("-");
			else if (labels && labels[i][j])
				printf("%c", labels[i][j]);
			else if (i == movei && j == movej)
				printf("*");
			else if (BOARD(i, j)== color)
			printf("O");
			else if (BOARD(i, j) == OTHER_COLOR(color))
			printf("X");
			else
			printf(".");
		}
		if (i == -1)
		printf("+\n");
		else
		printf("|\n");
	}
}

			/* Write the colon line of the pattern. */
static void write_colon_line(int move_type, char symmetry, char *text) {
	char *p;

	/* Locate a possible colon line in the sgf file comment. */
	if (!text)
		p = NULL;
	else if (*text == ':')
		p = text + 1;
	else {
		p = strstr(text, "\n:");
		if (p)
			p += 2;
	}

	printf(":%c,sF", symmetry);
	switch (move_type) {
	case URGENT:
		printf("U");
		break;
	case STANDARD:
		printf("J");
		break;
	case MINOR:
		printf("j");
		break;
	case TRICK:
		printf("T");
		break;
	case TENUKI_OK:
		printf("t");
		break;
	case ANTISUJI:
		printf("N");
		break;
	}

	if (p) {
		/* A little trick to guess whether the supplied colon line in the
		 * sgf file begins with a classification.
		 */
		if (strchr(p, '(')
				&& (!strchr(p, ',') || strchr(p, ',') > strchr(p, '(')))
			printf(",");
		while (*p != 0 && *p != '\n')
			fputc(*(p++), stdout);
	}
	printf("\n");
}

/* Check if the board and labels are symmetric. */
static int board_is_symmetric(int n, char labels[MAX_BOARD][MAX_BOARD]) {
	int i;
	int j;

	for (i = 0; i <= n; i++) {
		for (j = 0; j < i; j++) {
			if (BOARD(i, j)!= BOARD(j, i)
			|| (labels && labels[i][j] != labels[j][i]))
			return 0;
		}
	}

	return 1;
}

/* Write a pattern to stdout. */
static void make_pattern(int movei, int movej, int color, int marki, int markj,
		int multiple_marks, char labels[MAX_BOARD][MAX_BOARD], char *text,
		const char *prefix) {
	static int pattern_number = 0;
	int move_type;
	char symmetry = '8';

	pattern_number++;
	move_type = identify_move_type(text);

	printf("Pattern %s%d\n", prefix, pattern_number);

	/* Write comments. */
	write_selected_lines(text, '#');
	printf("\n");

	/* Write the main diagram. */
	write_diagram(movei, movej, color, marki, markj, NULL );
	printf("\n");

	/* Write the colon line. */
	if (movei == movej && marki == markj && board_is_symmetric(marki, labels))
		symmetry = '/';
	write_colon_line(move_type, symmetry, text);
	printf("\n");

	/* Write the constraint diagram if there are any labels, a
	 * constraint line, or an action line.
	 */
	if (labels || selected_line_exists(text, ';')
			|| selected_line_exists(text, '>')) {
		write_diagram(movei, movej, color, marki, markj, labels);

		printf("\n");

		/* Write constraint and action lines. */
		write_selected_lines(text, ';');
		write_selected_lines(text, '>');
		printf("\n");
	}

	printf("\n");

	/* Basic sanity checking. We do this at the end to simplify debugging. */
	if (multiple_marks)
		fprintf(stderr, "Warning: Multiple square marks in pattern %s%d\n",
				prefix, pattern_number);

	if (is_suicide(POS(movei, movej), color)) {
		fprintf(stderr, "Error: Illegal move in pattern %s%d\n", prefix,
				pattern_number);
		exit(EXIT_FAILURE);
	}
}

/* Analyze the node properties in order to make a pattern. Then make
 * recursive calls for child node and siblings.
 */
static void analyze_node(SGFNode *node, const char *prefix) {
	SGFProperty *prop;
	int i, j;
	char labels[MAX_BOARD][MAX_BOARD];
	int label_found = 0;
	int movei = -1;
	int movej = -1;
	int color = EMPTY;
	int marki = -1;
	int markj = -1;
	int multiple_marks = 0;
	char *comment = NULL;

	/* Clear the labels array. */
	memset(labels, 0, MAX_BOARD * MAX_BOARD);

	/* Check the node properties for a move, a square mark, labels, and
	 * a comment.
	 */
	for (prop = node->props; prop; prop = prop->next) {
		switch (prop->name) {
		case SGFSQ: /* Square */
		case SGFMA: /* Mark */
			if (marki != -1)
				multiple_marks = 1;
			else {
				get_moveXY(prop, &marki, &markj, boardsize);
				markj = boardsize - 1 - markj;
			}
			break;

		case SGFW: /* White move */
			color = WHITE;
			get_moveXY(prop, &movei, &movej, boardsize);
			movej = boardsize - 1 - movej;
			break;

		case SGFB: /* Black move */
			color = BLACK;
			get_moveXY(prop, &movei, &movej, boardsize);
			movej = boardsize - 1 - movej;
			break;

		case SGFLB: /* Label, with value like "mh:A" */
			get_moveXY(prop, &i, &j, boardsize);
			j = boardsize - 1 - j;
			gg_assert(prop->value[2] == ':');
			if (ON_BOARD2(i, j)) {
				labels[i][j] = prop->value[3];
				label_found = 1;
			}
			break;

		case SGFC: /* Comment */
			comment = prop->value;
			break;
		}
	}

	/* If we have a move and a square mark, produce a pattern. */
	if (SAFE_ON_BOARD(movei, movej) && ON_BOARD2(marki, markj))
		make_pattern(movei, movej, color, marki, markj, multiple_marks,
				(label_found ? labels : NULL ), comment, prefix);
	printf("For debug +++ \n");
	/* Traverse child, if any. */
	if (node->child) {
		if (SAFE_ON_BOARD(movei, movej))
			tryko(POS(movei, movej), color, NULL );
		analyze_node(node->child, prefix);
		if (SAFE_ON_BOARD(movei, movej))
			popgo();
	}

	/* Traverse sibling, if any. */
	if (node->next)
		analyze_node(node->next, prefix);
}

int showscore = 0;

/*
 * Display the board position when playing in ASCII.
 */

static void mip_ascii_showboard(void) {
	int i, j;
	char letterbar[64];
	int last_pos_was_move;
	int pos_is_move;
	int dead;
	int last_move = get_last_move();

	make_letterbar(board_size, letterbar);
	set_handicap_spots(board_size);

	printf("\n");
	printf("    White (O) has captured %d pieces\n", black_captured);
	printf("    Black (X) has captured %d pieces\n", white_captured);
	if (showscore) {
		if (current_score_estimate == NO_SCORE)
			printf("    No score estimate is available yet.\n");
		else if (current_score_estimate < 0)
			printf("    Estimated score: Black is ahead by %d\n",
					-current_score_estimate);
		else if (current_score_estimate > 0)
			printf("    Estimated score: White is ahead by %d\n",
					current_score_estimate);
		else
			printf("    Estimated score: Even!\n");
	}

	printf("\n");

	fflush(stdout);
	printf("%s", letterbar);

	if (get_last_player() != EMPTY) {
		gfprintf(stdout, "        Last move: %s %1m",
				get_last_player() == WHITE ? "White" : "Black", last_move);
	}

	printf("\n");
	fflush(stdout);

	for (i = 0; i < board_size; i++) {
		printf(" %2d", board_size - i);
		last_pos_was_move = 0;
		for (j = 0; j < board_size; j++) {
			if (POS(i, j) == last_move)
				pos_is_move = 128;
			else
				pos_is_move = 0;
			dead = (dragon_status(POS(i, j)) == DEAD) && showdead;
			switch (BOARD(i, j)+ pos_is_move + last_pos_was_move) {
				case EMPTY+128:
				case EMPTY:
				printf(" %c", hspots[i][j]);
				last_pos_was_move = 0;
				break;
				case BLACK:
				printf(" %c", dead ? 'x' : 'X');
				last_pos_was_move = 0;
				break;
				case WHITE:
				printf(" %c", dead ? 'o' : 'O');
				last_pos_was_move = 0;
				break;
				case BLACK+128:
				printf("(%c)", 'X');
				last_pos_was_move = 256;
				break;
				case WHITE+128:
				printf("(%c)", 'O');
				last_pos_was_move = 256;
				break;
				case EMPTY+256:
				printf("%c", hspots[i][j]);
				last_pos_was_move = 0;
				break;
				case BLACK+256:
				printf("%c", dead ? 'x' : 'X');
				last_pos_was_move = 0;
				break;
				case WHITE+256:
				printf("%c", dead ? 'o' : 'O');
				last_pos_was_move = 0;
				break;
				default:
				fprintf(stderr, "Illegal board value %d\n", (int) BOARD(i, j));
				exit(EXIT_FAILURE);
				break;
			}
		}

		if (last_pos_was_move == 0) {
			if (board_size > 10)
				printf(" %2d", board_size - i);
			else
				printf(" %1d", board_size - i);
		} else {
			if (board_size > 10)
				printf("%2d", board_size - i);
			else
				printf("%1d", board_size - i);
		}
		printf("\n");
	}

	fflush(stdout);
	printf("%s\n\n", letterbar);
	fflush(stdout);
	/*
	 if (clock_on) {
	 clock_print(WHITE);
	 clock_print(BLACK);
	 }
	 */

} /* end ascii_showboard */

/*
 * Initialize the structure.
 */

void gameinfo_clear(Gameinfo *gameinfo) {
	gnugo_clear_board(board_size);
	gameinfo->handicap = 0;
	gameinfo->to_move = BLACK;
	sgftree_clear(&gameinfo->game_record);

	/* Info relevant to the computer player. */
	gameinfo->computer_player = WHITE; /* Make an assumption. */
}

void process_sgf(char* infilename, char* number) {
	printf("Processs SGF file %s \n", infilename);
	Gameinfo gameinfo;
	SGFTree sgftree;

	gameinfo_clear(&gameinfo);
	sgftree_clear(&sgftree);

	if (!sgftree_readfile(&sgftree, infilename)) {
		fprintf(stderr, "Cannot open or parse '%s'\n", infilename);
		exit(EXIT_FAILURE);
	}
	/*
	if (gameinfo_play_sgftree_rot(&gameinfo, &sgftree, number, 0) == EMPTY) {
		fprintf(stderr, "Cannot load '%s'\n", infilename);
		exit(EXIT_FAILURE);
	}

	mip_ascii_showboard();*/

	sgfSimplePlayer(&gameinfo, &sgftree, number, 0);

}


int doNext(Gameinfo *gameinfo, SGFTree *tree)
{
	int orientation = 0;
	int next;

	int move;
	SGFProperty *prop;
	printf("\nNode property:");
	for (prop = tree->lastnode->props; prop; prop = prop->next) {
		//DEBUG(DEBUG_LOADSGF, "%c%c[%s]\n",
		//  prop->name & 0xff, (prop->name >> 8), prop->value);
		printf("%c%c[%s] - ",prop->name & 0xff, (prop->name >> 8), prop->value);
		switch (prop->name) {
		case SGFAB:
		case SGFAW:
			/* Generally the last move is unknown when the AB or AW
			 * properties are encountered. These are used to set up
			 * a board position (diagram) or to place handicap stones
			 * without reference to the order in which the stones are
			 * placed on the board.
			 */
			move = rotate1(get_sgfmove(prop), orientation);
			if (board[move] != EMPTY)
				gprintf(
						"Illegal SGF! attempt to add a stone at occupied point %1m\n",
						move);
			else
				add_stone(move, prop->name == SGFAB ? BLACK : WHITE);
			break;

		case SGFPL:
			/* Due to a bad comment in the SGF FF3 definition (in the
			 * "Alphabetical list of properties" section) some
			 * applications encode the colors with 1 for black and 2 for
			 * white.
			 */
			if (prop->value[0] == 'w' || prop->value[0] == 'W'
					|| prop->value[0] == '2')
				next = WHITE;
			else
				next = BLACK;
			/* following really should not be needed for proper sgf file */
			if (stones_on_board(GRAY) == 0 && next == WHITE) {
				place_fixed_handicap(gameinfo->handicap);
				sgfOverwritePropertyInt(tree->root, "HA", handicap);
			}
			break;

		case SGFW:
		case SGFB:
			next = prop->name == SGFW ? WHITE : BLACK;
			/* following really should not be needed for proper sgf file */
			if (stones_on_board(GRAY) == 0 && next == WHITE) {
				place_fixed_handicap(gameinfo->handicap);
				sgfOverwritePropertyInt(tree->root, "HA", handicap);
			}

			move = get_sgfmove(prop);
//				if (move == untilmove || movenum == until - 1) {
//					gameinfo->to_move = next;
//					/* go back so that variant will be added to the proper node */
//					sgftreeBack(tree);
//					return next;
//				}

			move = rotate1(move, orientation);
			if (move == PASS_MOVE || board[move] == EMPTY) {
				gnugo_play_move(move, next);
				next = OTHER_COLOR(next);
			} else {
				gprintf(
						"WARNING: Move off board or on occupied position found in sgf-file.\n");
				gprintf("Move at %1m ignored, trying to proceed.\n", move);
				gameinfo->to_move = next;
				return next;
			}

			break;

		case SGFIL:
			/* The IL property is not a standard SGF property but
			 * is used by GNU Go to mark illegal moves. If a move
			 * is found marked with the IL property which is a ko
			 * capture then that ko capture is deemed illegal and
			 * (board_ko_i, board_ko_j) is set to the location of
			 * the ko.
			 */
			move = rotate1(get_sgfmove(prop), orientation);

			if (board_size > 1) {
				int move_color;

				if (ON_BOARD(NORTH(move)))
					move_color = OTHER_COLOR(board[NORTH(move)]);
				else
					move_color = OTHER_COLOR(board[SOUTH(move)]);
				if (is_ko(move, move_color, NULL ))
					board_ko_pos = move;
			}
			break;
		}
	}
	return 0;
}

int sgfSimplePlayer(Gameinfo *gameinfo, SGFTree *tree,
		const char *untilstr, int orientation) {
	int bs;
	int next = BLACK;
	int untilmove = -1; /* Neither a valid move nor pass. */
	int until = 9999;
	char line[80];
	if (!sgfGetIntProperty(tree->root, "SZ", &bs))
		bs = 19;

	if (!check_boardsize(bs, stderr))
		return EMPTY;

	handicap = 0;
	if (sgfGetIntProperty(tree->root, "HA", &handicap) && handicap > 1)
		next = WHITE;
	gameinfo->handicap = handicap;

	if (handicap > bs * bs - 1 || handicap < 0) {
		gprintf(" Handicap HA[%d] is unreasonable.\n Modify SGF file.\n",
				handicap);
		return EMPTY;
	}

	gnugo_clear_board(bs);

	if (!sgfGetFloatProperty(tree->root, "KM", &komi)) {
		if (gameinfo->handicap == 0)
			komi = 5.5;
		else
			komi = 0.5;
	}

	/* Now we can safely parse the until string (which depends on board size). */
	if (untilstr) {
		if (*untilstr > '0' && *untilstr <= '9') {
			until = atoi(untilstr);
			//DEBUG(DEBUG_LOADSGF, "Loading until move %d\n", until);
		} else {
			untilmove = string_to_location(board_size, untilstr);
			// DEBUG(DEBUG_LOADSGF, "Loading until move at %1m\n", untilmove);
		}
	}

	/* Finally, we iterate over all the properties of all the
	 * nodes, actioning them. We follow only the 'child' pointers,
	 * as we have no interest in variations.
	 *
	 * The sgf routines map AB[aa][bb][cc] into AB[aa]AB[bb]AB[cc]
	 */
	for (tree->lastnode = NULL ; sgftreeForward(tree);) {
		//show variation
		//TODO
		//get input
		//TODO
		fgets(line,80,stdin);
		//playmove
		//TODO
		doNext(gameinfo,tree);
		//show board
		mip_ascii_showboard();
	}

	gameinfo->to_move = next;
	return next;
}

int main(int argc, char *argv[]) {
	const char *filename;
	const char *number;

	SGFNode *sgf;

	/* Check number of arguments. */
	if (argc != 3) {
		fprintf(stderr, USAGE);
		exit(EXIT_FAILURE);
	}

	number = argv[2];
	filename = argv[1];

	/* Read the sgf file into a tree in memory. */
	sgf = readsgffile(filename);
	if (!sgf) {
		fprintf(stderr, "%s: Couldn't open sgf file %s.\n", argv[0], filename);
		exit(EXIT_FAILURE);
	}

#define PREAMBLE "TMIP\N"

	printf(PREAMBLE);
	printf("attribute_map general\n\n");

	/* Call the engine to setup and clear the board. */
	board_size = MAX_BOARD;
	clear_board();

	/* Determine board size of the file. */
	if (!sgfGetIntProperty(sgf, "SZ", &boardsize)) {
		fprintf(stderr, "joseki: error: can't determine file board size\n");
		return 1;
	}
	printf("The board Size %d \n", boardsize);

	/* Walk through the tree and make patterns. */
	//analyze_node(sgf, prefix);
	process_sgf(filename, number);

	return 0;
}

/* Check whether we can accept a certain boardsize. Set out to NULL to
 * suppress informative messages. Return 1 for an acceptable
 * boardsize, 0 otherwise.
 */
int check_boardsize(int boardsize, FILE *out) {
	int max_board = MAX_BOARD;
	//if (use_monte_carlo_genmove && max_board > 9)
	//  max_board = 9;

	if (boardsize < MIN_BOARD || boardsize > max_board) {
		if (out) {
			fprintf(out, "Unsupported board size: %d. ", boardsize);
			if (boardsize < MIN_BOARD)
				fprintf(out, "Min size is %d.\n", MIN_BOARD);
			else {
				fprintf(out, "Max size is %d", max_board);
				if (max_board < MAX_BOARD)
					fprintf(out, " (%d without --monte-carlo)", MAX_BOARD);
				fprintf(out, ".\n");
			}
			fprintf(out, "Try `gnugo --help' for more information.\n");
		}
		return 0;
	}

	return 1;
}

/*
 * Clear the board.
 */
void gnugo_clear_board(int boardsize) {
	board_size = boardsize;
	clear_board();
//  init_timers();
#if 0
	if (metamachine && oracle_exists)
	oracle_clear_board(boardsize);
#endif
}

/* Play a move and start the clock */

void gnugo_play_move(int move, int color) {
#if ORACLE
	if (oracle_exists)
	oracle_play_move(move, color);
	else
	play_move(move, color);
#else
	play_move(move, color);
#endif
//  clock_push_button(color);
}

/*
 * Play the moves in an SGF tree. Walk the main variation, actioning
 * the properties into the playing board.
 *
 * Returns the color of the next move to be made. The returned color
 * being EMPTY signals a failure to load the file.
 *
 * Head is an sgf tree.
 * Untilstr is an optional string of the form either 'L12' or '120'
 * which tells it to stop playing at that move or move-number.
 * When debugging, this is the location of the move being examined.
 */

int gameinfo_play_sgftree_rot(Gameinfo *gameinfo, SGFTree *tree,
		const char *untilstr, int orientation) {
	int bs;
	int next = BLACK;
	int untilmove = -1; /* Neither a valid move nor pass. */
	int until = 9999;

	if (!sgfGetIntProperty(tree->root, "SZ", &bs))
		bs = 19;

	if (!check_boardsize(bs, stderr))
		return EMPTY;

	handicap = 0;
	if (sgfGetIntProperty(tree->root, "HA", &handicap) && handicap > 1)
		next = WHITE;
	gameinfo->handicap = handicap;

	if (handicap > bs * bs - 1 || handicap < 0) {
		gprintf(" Handicap HA[%d] is unreasonable.\n Modify SGF file.\n",
				handicap);
		return EMPTY;
	}

	gnugo_clear_board(bs);

	if (!sgfGetFloatProperty(tree->root, "KM", &komi)) {
		if (gameinfo->handicap == 0)
			komi = 5.5;
		else
			komi = 0.5;
	}

	/* Now we can safely parse the until string (which depends on board size). */
	if (untilstr) {
		if (*untilstr > '0' && *untilstr <= '9') {
			until = atoi(untilstr);
			//DEBUG(DEBUG_LOADSGF, "Loading until move %d\n", until);
		} else {
			untilmove = string_to_location(board_size, untilstr);
			// DEBUG(DEBUG_LOADSGF, "Loading until move at %1m\n", untilmove);
		}
	}

	/* Finally, we iterate over all the properties of all the
	 * nodes, actioning them. We follow only the 'child' pointers,
	 * as we have no interest in variations.
	 *
	 * The sgf routines map AB[aa][bb][cc] into AB[aa]AB[bb]AB[cc]
	 */
	for (tree->lastnode = NULL ; sgftreeForward(tree);) {
		SGFProperty *prop;
		int move;

		for (prop = tree->lastnode->props; prop; prop = prop->next) {
			//DEBUG(DEBUG_LOADSGF, "%c%c[%s]\n",
			//  prop->name & 0xff, (prop->name >> 8), prop->value);
			switch (prop->name) {
			case SGFAB:
			case SGFAW:
				/* Generally the last move is unknown when the AB or AW
				 * properties are encountered. These are used to set up
				 * a board position (diagram) or to place handicap stones
				 * without reference to the order in which the stones are
				 * placed on the board.
				 */
				move = rotate1(get_sgfmove(prop), orientation);
				if (board[move] != EMPTY)
					gprintf(
							"Illegal SGF! attempt to add a stone at occupied point %1m\n",
							move);
				else
					add_stone(move, prop->name == SGFAB ? BLACK : WHITE);
				break;

			case SGFPL:
				/* Due to a bad comment in the SGF FF3 definition (in the
				 * "Alphabetical list of properties" section) some
				 * applications encode the colors with 1 for black and 2 for
				 * white.
				 */
				if (prop->value[0] == 'w' || prop->value[0] == 'W'
						|| prop->value[0] == '2')
					next = WHITE;
				else
					next = BLACK;
				/* following really should not be needed for proper sgf file */
				if (stones_on_board(GRAY) == 0 && next == WHITE) {
					place_fixed_handicap(gameinfo->handicap);
					sgfOverwritePropertyInt(tree->root, "HA", handicap);
				}
				break;

			case SGFW:
			case SGFB:
				next = prop->name == SGFW ? WHITE : BLACK;
				/* following really should not be needed for proper sgf file */
				if (stones_on_board(GRAY) == 0 && next == WHITE) {
					place_fixed_handicap(gameinfo->handicap);
					sgfOverwritePropertyInt(tree->root, "HA", handicap);
				}

				move = get_sgfmove(prop);
				if (move == untilmove || movenum == until - 1) {
					gameinfo->to_move = next;
					/* go back so that variant will be added to the proper node */
					sgftreeBack(tree);
					return next;
				}

				move = rotate1(move, orientation);
				if (move == PASS_MOVE || board[move] == EMPTY) {
					gnugo_play_move(move, next);
					next = OTHER_COLOR(next);
				} else {
					gprintf(
							"WARNING: Move off board or on occupied position found in sgf-file.\n");
					gprintf("Move at %1m ignored, trying to proceed.\n", move);
					gameinfo->to_move = next;
					return next;
				}

				break;

			case SGFIL:
				/* The IL property is not a standard SGF property but
				 * is used by GNU Go to mark illegal moves. If a move
				 * is found marked with the IL property which is a ko
				 * capture then that ko capture is deemed illegal and
				 * (board_ko_i, board_ko_j) is set to the location of
				 * the ko.
				 */
				move = rotate1(get_sgfmove(prop), orientation);

				if (board_size > 1) {
					int move_color;

					if (ON_BOARD(NORTH(move)))
						move_color = OTHER_COLOR(board[NORTH(move)]);
					else
						move_color = OTHER_COLOR(board[SOUTH(move)]);
					if (is_ko(move, move_color, NULL ))
						board_ko_pos = move;
				}
				break;
			}
		}
	}

	gameinfo->to_move = next;
	return next;
}

/* Same as previous function, using standard orientation */

int gameinfo_play_sgftree(Gameinfo *gameinfo, SGFTree *tree,
		const char *untilstr) {
	return gameinfo_play_sgftree_rot(gameinfo, tree, untilstr, 0);
}

/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
