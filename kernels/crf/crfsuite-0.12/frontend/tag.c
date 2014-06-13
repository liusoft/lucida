/*
 *        Tag command for CRFsuite frontend.
 *
 * Copyright (c) 2007-2010, Naoaki Okazaki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of the authors nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id$ */

#include <os.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <crfsuite.h>
#include "option.h"
#include "iwa.h"

#include <sys/time.h>


#include <pthread.h>

#define NTHREADS      8


#define    SAFE_RELEASE(obj)    if ((obj) != NULL) { (obj)->release(obj); (obj) = NULL; }

void show_copyright(FILE *fp);

crfsuite_tagger_t *tagger;
crfsuite_instance_t **inst_vect;
int N = 0;

/*typedef struct {
  int *array;
  size_t used;
  size_t size;
} Array;

void initArray(Array *a, size_t initialSize) {
  a->array = (int *)malloc(initialSize * sizeof(int));
  a->used = 0;
  a->size = initialSize;
}

void insertArray(Array *a, int element) {
  if (a->used == a->size) {
    a->size *= 2;
    a->array = (int *)realloc(a->array, a->size * sizeof(int));
  }
  a->array[a->used++] = element;
}

void freeArray(Array *a) {
  free(a->array);
  a->array = NULL;
  a->used = a->size = 0;
}*/


typedef struct {
    char *input;
    char *model;
    int evaluate;
    int probability;
    int marginal;
    int quiet;
    int reference;
    int help;

    int num_params;
    char **params;

    FILE *fpi;
    FILE *fpo;
    FILE *fpe;
} tagger_option_t;

static char* mystrdup(const char *src)
{
    char *dst = (char*)malloc(strlen(src)+1);
    if (dst != NULL) {
        strcpy(dst, src);
    }
    return dst;
}

static void tagger_option_init(tagger_option_t* opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->fpi = stdin;
    opt->fpo = stdout;
    opt->fpe = stderr;
    opt->model = mystrdup("");
}

static void tagger_option_finish(tagger_option_t* opt)
{
    int i;

    free(opt->input);
    free(opt->model);
    for (i = 0;i < opt->num_params;++i) {
        free(opt->params[i]);
    }
    free(opt->params);
}

BEGIN_OPTION_MAP(parse_tagger_options, tagger_option_t)

    ON_OPTION_WITH_ARG(SHORTOPT('m') || LONGOPT("model"))
        free(opt->model);
        opt->model = mystrdup(arg);

    ON_OPTION(SHORTOPT('t') || LONGOPT("test"))
        opt->evaluate = 1;

    ON_OPTION(SHORTOPT('r') || LONGOPT("reference"))
        opt->reference = 1;

    ON_OPTION(SHORTOPT('p') || LONGOPT("probability"))
        opt->probability = 1;

    ON_OPTION(SHORTOPT('i') || LONGOPT("marginal"))
        opt->marginal = 1;

    ON_OPTION(SHORTOPT('q') || LONGOPT("quiet"))
        opt->quiet = 1;

    ON_OPTION(SHORTOPT('h') || LONGOPT("help"))
        opt->help = 1;

    ON_OPTION_WITH_ARG(SHORTOPT('p') || LONGOPT("param"))
        opt->params = (char **)realloc(opt->params, sizeof(char*) * (opt->num_params + 1));
        opt->params[opt->num_params] = mystrdup(arg);
        ++opt->num_params;

END_OPTION_MAP()

static void show_usage(FILE *fp, const char *argv0, const char *command)
{
    fprintf(fp, "USAGE: %s %s [OPTIONS] [DATA]\n", argv0, command);
    fprintf(fp, "Assign suitable labels to the instances in the data set given by a file (DATA).\n");
    fprintf(fp, "If the argument DATA is omitted or '-', this utility reads a data from STDIN.\n");
    fprintf(fp, "Evaluate the performance of the model on labeled instances (with -t option).\n");
    fprintf(fp, "\n");
    fprintf(fp, "OPTIONS:\n");
    fprintf(fp, "    -m, --model=MODEL   Read a model from a file (MODEL)\n");
    fprintf(fp, "    -t, --test          Report the performance of the model on the data\n");
    fprintf(fp, "    -r, --reference     Output the reference labels in the input data\n");
    fprintf(fp, "    -p, --probability   Output the probability of the label sequences\n");
    fprintf(fp, "    -i, --marginal      Output the marginal probabilities of items\n");
    fprintf(fp, "    -q, --quiet         Suppress tagging results (useful for test mode)\n");
    fprintf(fp, "    -h, --help          Show the usage of this command and exit\n");
}



static void
output_result(
    FILE *fpo,
    crfsuite_tagger_t *tagger,
    const crfsuite_instance_t *inst,
    int *output,
    crfsuite_dictionary_t *labels,
    floatval_t score,
    const tagger_option_t* opt
    )
{
    int i;

    if (opt->probability) {
        floatval_t lognorm;
        tagger->lognorm(tagger, &lognorm);
        fprintf(fpo, "@probability\t%f\n", exp(score - lognorm));
    }

    for (i = 0;i < inst->num_items;++i) {
        const char *label = NULL;

        if (opt->reference) {
            labels->to_string(labels, inst->labels[i], &label);
            fprintf(fpo, "%s\t", label);
            labels->free(labels, label);
        }

        labels->to_string(labels, output[i], &label);
        fprintf(fpo, "%s", label);
        labels->free(labels, label);

        if (opt->marginal) {
            floatval_t prob;
            tagger->marginal_point(tagger, output[i], i, &prob);
            fprintf(fpo, ":%f", prob);
        }

        fprintf(fpo, "\n");
    }
    fprintf(fpo, "\n");
}

static void
output_instance(
    FILE *fpo,
    const crfsuite_instance_t *inst,
    crfsuite_dictionary_t *labels,
    crfsuite_dictionary_t *attrs
    )
{
    int i, j;

    for (i = 0;i < inst->num_items;++i) {
        const char *label = NULL;
        labels->to_string(labels, inst->labels[i], &label);
        fprintf(fpo, "%s", label);
        labels->free(labels, label);

        for (j = 0;j < inst->items[i].num_contents;++j) {
            const char *attr = NULL;
            attrs->to_string(attrs, inst->items[i].contents[j].aid, &attr);
            fprintf(fpo, "\t%s:%f", attr, inst->items[i].contents[j].value);
            attrs->free(attrs, attr);
        }

        fprintf(fpo, "\n");
    }
    fprintf(fpo, "\n");
}

static int message_callback(void *instance, const char *format, va_list args)
{
    FILE *fp = (FILE*)instance;
    vfprintf(fp, format, args);
    fflush(fp);
    return 0;
}

static int tag_read(tagger_option_t* opt, crfsuite_model_t* model)
{
    int L = 0, ret = 0, lid = -1;
    clock_t clk0, clk1;
   // crfsuite_instance_t *inst;
    crfsuite_item_t *item;
    crfsuite_attribute_t cont;
    crfsuite_evaluation_t eval;
    char *comment = NULL;
    iwa_t* iwa = NULL;
    const iwa_token_t* token = NULL;
    crfsuite_tagger_t *tagger = NULL;
    crfsuite_dictionary_t *attrs = NULL, *labels = NULL;
    FILE *fp = NULL, *fpi = opt->fpi, *fpo = opt->fpo, *fpe = opt->fpe;
    int cur_inst_size = 10000;
    int k;
    int M;

    /* Obtain the dictionary interface representing the labels in the model. */
    if (ret = model->get_labels(model, &labels)) {
        goto force_exit;
    }

    /* Obtain the dictionary interface representing the attributes in the model. */
    if (ret = model->get_attrs(model, &attrs)) {
        goto force_exit;
    }

    /* Obtain the tagger interface. */
    if (ret = model->get_tagger(model, &tagger)) {
        goto force_exit;
    }

//    item_vect = (crfsuite_item_t **)malloc(cur_inst_size * sizeof(crfsuite_item_t *));
//    item_vect[N] = (crfsuite_item_t *)malloc(sizeof(crfsuite_item_t));

    inst_vect = (crfsuite_instance_t **)malloc(cur_inst_size * sizeof(crfsuite_instance_t *));
    inst_vect[N] = (crfsuite_instance_t *)malloc(sizeof(crfsuite_instance_t));

    /* Initialize the objects for instance and evaluation. */
    L = labels->num(labels);
    crfsuite_instance_init(inst_vect[N]);
    crfsuite_evaluation_init(&eval, L);

    /* Open the stream for the input data. */
    fp = (strcmp(opt->input, "-") == 0) ? fpi : fopen(opt->input, "r");
    if (fp == NULL) {
        fprintf(fpe, "ERROR: failed to open the stream for the input data,\n");
        fprintf(fpe, "  %s\n", opt->input);
        ret = 1;
        goto force_exit;
    }

    /* Open a IWA reader. */
    iwa = iwa_reader(fp);
    if (iwa == NULL) {
        fprintf(fpe, "ERROR: Failed to initialize the parser for the input data.\n");
        ret = 1;
        goto force_exit;
    }

    /* Read the input data and assign labels. */
    clk0 = clock();
    while (token = iwa_read(iwa), token != NULL) {
        switch (token->type) {
        case IWA_BOI:
            /* Initialize an item. */
            lid = -1;
            item = (crfsuite_item_t *)malloc(sizeof(crfsuite_item_t));
            crfsuite_item_init(item);
            free(comment);
            comment = NULL;
            break;
        case IWA_EOI:
            /* Append the item to the instance. */
            crfsuite_instance_append(inst_vect[N], item, lid);
            //crfsuite_item_finish(item_vect[N]);
            break;
        case IWA_ITEM:
            if (lid == -1) {
                /* The first field in a line presents a label. */
                lid = labels->to_id(labels, token->attr);
                if (lid < 0) lid = L;    /* #L stands for a unknown label. */
            } else {
                /* Fields after the first field present attributes. */
                int aid = attrs->to_id(attrs, token->attr);
                /* Ignore attributes 'unknown' to the model. */
                if (0 <= aid) {
                    /* Associate the attribute with the current item. */
                    if (token->value && *token->value) {
                        crfsuite_attribute_set(&cont, aid, atof(token->value));
                    } else {
                        crfsuite_attribute_set(&cont, aid, 1.0);
                    }
                    crfsuite_item_append_attribute(item, &cont);
                }
            }
            break;
        case IWA_NONE:
        case IWA_EOF:

            if (!crfsuite_instance_empty(inst_vect[N])) {

                 N++;

                 inst_vect[N] = (crfsuite_instance_t *)malloc(sizeof(crfsuite_instance_t));

                 crfsuite_instance_init(inst_vect[N]);


    //fprintf(fpo, "N = %d\n", N);
   // fflush(fpo);


                 /*if (N > cur_inst_size)  {
                    cur_inst_size *= 2;
                    inst_vect = (crfsuite_instance_t **)realloc(inst_vect, cur_inst_size * sizeof(crfsuite_instance_t *));

                 }*/

            }
            break;
           /* if (!crfsuite_instance_empty(&inst)) {
                // Initialize the object to receive the tagging result. 
                floatval_t score = 0;
                int *output = calloc(sizeof(int), inst.num_items);

                // Set the instance to the tagger. 
                if ((ret = tagger->set(tagger, &inst))) {
                    goto force_exit;
                }

                // Obtain the viterbi label sequence. 
                if ((ret = tagger->viterbi(tagger, output, &score))) {
                    goto force_exit;
                }

                ++N;

                // Accumulate the tagging performance. 
                if (opt->evaluate) {
                    crfsuite_evaluation_accmulate(&eval, inst.labels, output, inst.num_items);
                }

                if (!opt->quiet) {
                    output_result(fpo, tagger, &inst, output, labels, score, opt);
                }


                free(output);
                crfsuite_instance_finish(&inst);
            }
            break;*/
        }
    }
    clk1 = clock();

  //  fprintf(fpo, "N = %d\n", N);
   // fflush(fpo);

    /* Compute the performance if specified. */
    if (opt->evaluate) {
        double sec = (clk1 - clk0) / (double)CLOCKS_PER_SEC;
        crfsuite_evaluation_finalize(&eval);
        crfsuite_evaluation_output(&eval, labels, message_callback, stdout);
        fprintf(fpo, "Elapsed time: %f [sec] (%.1f [instance/sec])\n", sec, N / sec);
    }

force_exit:
    /* Close the IWA parser. */
    iwa_delete(iwa);
    iwa = NULL;

    /* Close the input stream if necessary. */
    if (fp != NULL && fp != fpi) {
        fclose(fp);
        fp = NULL;
    }

    free(comment);
    //for (k=0; k < N;k++)
    //  crfsuite_instance_finish(inst_vect[k]);
//    crfsuite_evaluation_finish(&eval);

    //SAFE_RELEASE(tagger);
    //SAFE_RELEASE(attrs);
    //SAFE_RELEASE(labels);

    return ret;
}

float calculateMiliseconds(struct timeval t1, struct timeval t2) {
	float elapsedTime;
	elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;
	elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;
	return elapsedTime;
}


void *tag_thread(void *tid)
{

	int k, start, *mytid, end;

        int iterations = N / NTHREADS;

	mytid = (int *) tid;
	start = (*mytid * iterations);
	end = start + iterations;
//	printf ("Thread %d doing iterations %d to %d\n",*mytid,start,end-1);

	 for (k=start; k < end ; k++) {

                floatval_t score = 0;
                int *output = calloc(sizeof(int), inst_vect[k]->num_items);

                // Set the instance to the tagger. 
                tagger->set(tagger, inst_vect[k]);

                // Obtain the viterbi label sequence. 
                tagger->viterbi(tagger, output, &score);
                   
                //free(output);
    }

}

int main_tag(int argc, char *argv[], const char *argv0)
{
    int ret = 0, arg_used = 0;
    tagger_option_t opt;
    const char *command = argv[0];
    FILE *fp = NULL, *fpi = stdin, *fpo = stdout, *fpe = stderr;
    crfsuite_model_t *model = NULL;
    crfsuite_data_t* input_data;
    int n, k;

	struct timeval t1,t2;
	float cuda_elapsedTime;
	float cpu_elapsedTime;
	float par_elapsedTime;


	  int i, start, tids[NTHREADS];
	  pthread_t threads[NTHREADS];
	  pthread_attr_t attr;

    /* Parse the command-line option. */
    tagger_option_init(&opt);
    arg_used = option_parse(++argv, --argc, parse_tagger_options, &opt);
    if (arg_used < 0) {
        ret = 1;
        goto force_exit;
    }

    /* Show the help message for this command if specified. */
    if (opt.help) {
        show_copyright(fpo);
        show_usage(fpo, argv0, command);
        goto force_exit;
    }

    /* Set an input file. */
    if (arg_used < argc) {
        opt.input = mystrdup(argv[arg_used]);
    } else {
        opt.input = mystrdup("-");    /* STDIN. */
    }

    /* Read the model. */
    if (opt.model != NULL) {
        /* Create a model instance corresponding to the model file. */
        if (ret = crfsuite_create_instance_from_file(opt.model, (void**)&model)) {
            goto force_exit;
        }

       /* Open the stream for the input data. */
       fpi = (strcmp(opt.input, "-") == 0) ? fpi : fopen(opt.input, "r");
       if (fpi == NULL) {
           fprintf(fpo, "ERROR: failed to open the stream for the input data,\n");
           fprintf(fpo, "  %s\n", opt.input);
           ret = 1;
           goto force_exit;
       }

        /* read input data */
       // n = read_data(fpi, fpo, &input_data, 0);
       // fprintf(fpo, "Number of instances: %d\n", n);

        /* read the input data. */
        tag_read(&opt, model);


//      int **output_vect 


       /* Obtain the tagger interface. */
       if (ret = model->get_tagger(model, &tagger)) {
           goto force_exit;
       }

    // SEQ ALGO

   // fprintf(fpo, "%d\n", inst_vect[0]->num_items);
    fprintf(fpo, "N = %d\n", N);

	gettimeofday(&t1, NULL);

//    int **output_cpu = (int **)malloc(N * sizeof(int *));

//    int *score_cpu = 

    for (k=0; k < N;k++) {

                floatval_t score = 0;
                int *output1 = calloc(sizeof(int), inst_vect[k]->num_items);

                // Set the instance to the tagger. 
                if ((ret = tagger->set(tagger, inst_vect[k]))) {
                    goto force_exit;
                }

                // Obtain the viterbi label sequence. 
                if ((ret = tagger->viterbi(tagger, output1, &score))) {
                    goto force_exit;
                }

              //  output_cpu[k] = output1;
  //            score_cpu[k] = score;

              //  free(output);
    }

      gettimeofday(&t2, NULL);

      cpu_elapsedTime = calculateMiliseconds(t1,t2);
	  printf("\nCPU SEQ Time=%4.3f ms\n",  cpu_elapsedTime);


    // PTHREAD	

    int **output_pthread = (int **)malloc(N * sizeof(int *));

	  pthread_attr_init(&attr);
	  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	  for (i=0; i<NTHREADS; i++) {
	    tids[i] = i;
	    pthread_create(&threads[i], &attr, tag_thread, (void *) &tids[i]);
	  }

         gettimeofday(&t1, NULL);

	//  printf ("Waiting for threads to finish.");
	  for (i=0; i<NTHREADS; i++) {
	    pthread_join(threads[i], NULL);
	  }
	//  printf("Done.");

      gettimeofday(&t2, NULL);

      par_elapsedTime = calculateMiliseconds(t1,t2);
	  printf("\nCPU Par Time=%4.3f ms\n",  par_elapsedTime);



        // free inst vect
   //     for (k=0; k < N;k++) 
 //           crfsuite_instance_finish(inst_vect[k]);

    }

force_exit:
    SAFE_RELEASE(model);
    tagger_option_finish(&opt);
    return ret;
}